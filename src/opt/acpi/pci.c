#include <arch/mmu.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/util.h>
#include <kernel/opt/acpi.h>
#include <kernel/opt/pci.h>
#include <kernel/platform/mmu.h>
#include <limits.h>
#include <vendor/acpi.h>

// TAKEN: Mathewnd/Astral
#define MCFG_MAPPING_SIZE(buscount) (4096UL * 8UL * 32UL * (buscount))

static struct acpi_mcfg_allocation *g_mcfg_entries = NULL;
static usize g_mcfg_num_entries = 0;

// TODO: figure if this is used frequently and if so, use radixttree for
// constant time lookups
static inline struct acpi_mcfg_allocation *mcfg_getentry(int bus) {
    for (usize i = 0; i < g_mcfg_num_entries; i++) {
        struct acpi_mcfg_allocation *entry = &g_mcfg_entries[i];
        if (bus >= entry->start_bus && bus <= entry->end_bus) return entry;
    }

    return NULL;
}

// NOTE: this returns the address after aligning the offset to the lower 4-byte
// multiple. So to read at unaligned offsets, additional logic is needed. All
// these alignment things are very weird, there may be some requirement when
// accessing MCFG that either I'm not aware of, or is only applicable to
// architectures other than x86-64
static inline volatile void *mcfg_getaddr(const PCIAddr *addr, usize off) {
    struct acpi_mcfg_allocation *entry = mcfg_getentry(addr->bus);
    if (entry == NULL) return NULL;

    uptr base = (uptr)entry->address;
    return (volatile void *)((uptr)base +
                             (((addr->bus - entry->start_bus) << 20) |
                              (addr->device << 15) | (addr->function << 12) |
                              (off & ~0x3)));
}

static u32 mcfg_read32_at(volatile u32 *address, usize off) {
    u32 full_value = *address;
    usize internal_off = (off & 0b11) * CHAR_BIT;
    return full_value >> internal_off;
}

static u32 mcfg_read32(const PCIAddr *addr, usize off) {
    volatile u32 *address = mcfg_getaddr(addr, off);
    if (address == NULL) return 0xffffffff;
    return mcfg_read32_at(address, off);
}

static u16 mcfg_read16(const PCIAddr *addr, usize off) {
    u32 word = mcfg_read32(addr, off);
    return (u16)(word & 0xffff);
}

static u8 mcfg_read8(const PCIAddr *addr, usize off) {
    u32 word = mcfg_read32(addr, off);
    return (u8)(word & 0xff);
}

static void mcfg_write32(const PCIAddr *addr, usize off, u32 val) {
    volatile u32 *address = mcfg_getaddr(addr, off);
    if (address == NULL) return;
    *address = val;
}

static void mcfg_write16(const PCIAddr *addr, usize off, u16 val) {
    volatile u32 *address = mcfg_getaddr(addr, off);
    if (address == NULL) return;

    u32 old = *address;
    usize internal_off = (off & 0b11) * CHAR_BIT;
    old &= ~(0xffffUL << internal_off);
    *address = old | (val << internal_off);
}

static void mcfg_write8(const PCIAddr *addr, usize off, u8 val) {
    volatile u8 *address = mcfg_getaddr(addr, off);
    if (address != NULL) *address = val;

    u32 old = *address;
    usize internal_off = (off & 0b11) * CHAR_BIT;
    old &= ~(0xffUL << internal_off);
    *address = old | (val << internal_off);
}

static PCIOverrides g_mcfg_overrides = {.read8 = mcfg_read8,
                                        .read16 = mcfg_read16,
                                        .read32 = mcfg_read32,
                                        .write8 = mcfg_write8,
                                        .write16 = mcfg_write16,
                                        .write32 = mcfg_write32};

int acpi_init_pci(void) {
    log("initializing mcfg");

    ACPITable tbl;
    int ret = acpi_impl_find_table_by_signature(ACPI_MCFG_SIGNATURE, &tbl);
    if (ret != 0) goto end;

    log_debug("found mcfg table at phys=%p, virt = %p", tbl.phys_addr,
              tbl.virt_addr);

    struct acpi_mcfg *mcfg = tbl.phys_addr;
    g_mcfg_num_entries = (mcfg->hdr.length - sizeof(struct acpi_sdt_hdr)) /
        sizeof(struct acpi_mcfg_allocation);

    void *entries_phys = pmm_allocn(
        g_mcfg_num_entries * sizeof(struct acpi_mcfg_allocation) / PAGE_SIZE +
        1);
    g_mcfg_entries = (void *)PHYS_TO_HHDM(entries_phys);
    memcpy(g_mcfg_entries, mcfg->entries,
           g_mcfg_num_entries * sizeof(struct acpi_mcfg_allocation));

    for (usize i = 0; i < g_mcfg_num_entries; i++) {
        void *phys_addr = (void *)g_mcfg_entries[i].address;
        uptr page_off = (uptr)phys_addr % PAGE_SIZE;
        usize mapping_size = page_off +
            MCFG_MAPPING_SIZE(g_mcfg_entries[i].end_bus -
                              g_mcfg_entries[i].start_bus + 1);

        g_mcfg_entries[i].address = (uptr)vmm_map(
            NULL, NULL, phys_addr, mapping_size,
            VMM_FLAGS_NOHINT | VMM_FLAGS_PHYSICAL,
            MMU_FLAGS_READ | MMU_FLAGS_WRITE | MMU_FLAGS_NOEXEC);
    }

    // TODO: unref the table
    pci_set_overrides(&g_mcfg_overrides);

    for (usize i = 0; i < g_mcfg_num_entries; i++) {
        struct acpi_mcfg_allocation *entry = &g_mcfg_entries[i];
        pci_enumate(entry->start_bus, entry->end_bus);
    }

end:
    return ret;
}
