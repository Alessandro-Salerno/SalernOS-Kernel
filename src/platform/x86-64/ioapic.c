#include <errno.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/util.h>
#include <kernel/opt/acpi.h>
#include <kernel/opt/uacpi.h>
#include <kernel/platform/mmu.h>
#include <vendor/acpi.h>

// CREDIT: Mathewnd/Astral
// TAKEN: Mathewnd/Astral
// NOTE: both CREDIT and TAKEN since some bits are outright taken and others are
// inspired by Astral

#define MAX_IOAPICS 32

#define IOAPIC_REG_ID          0
#define IOAPIC_REG_ENTRY_COUNT 1
#define IOAPIC_REG_PRIORITY    2
#define IOAPIC_REG_ENTRY       0x10

struct IOAPICDevice {
    void *addr;
    int base;
    int top;
};

static struct acpi_madt *MADTTable;
static struct IOAPICDevice g_ioapic_array[MAX_IOAPICS];
static struct acpi_entry_hdr *g_ioapic_list_start;
static struct acpi_entry_hdr *g_ioapic_list_end;

static usize g_override_count;
static usize g_ioapic_count;
static Spinlock g_ioapic_lock = SPINLOCK_NEW();

static inline u32 ioapic_read(void *ioapic, int reg) {
    volatile u32 *apic = ioapic;
    *apic = reg & 0xff;
    return *(apic + 4);
}

static inline void ioapic_write(void *ioapic, int reg, u32 v) {
    volatile u32 *apic = ioapic;
    *apic = reg & 0xff;
    *(apic + 4) = v;
}

static struct IOAPICDevice *ioapic_by_gsi(int gsi) {
    for (usize i = 0; i < g_ioapic_count; ++i) {
        struct IOAPICDevice *ioapic = &g_ioapic_array[i];
        if (gsi >= ioapic->base && gsi < ioapic->top) return ioapic;
    }

    return NULL;
}

static inline struct acpi_entry_hdr *
madt_getnext(struct acpi_entry_hdr *header) {
    uptr ptr = (uptr)header;
    return (struct acpi_entry_hdr *)(ptr + header->length);
}

static inline void *madt_getentry(int type, usize n) {
    struct acpi_entry_hdr *entry = g_ioapic_list_start;

    while (entry < g_ioapic_list_end) {
        if (entry->type == type) {
            if (n-- == 0) return entry;
        }
        entry = madt_getnext(entry);
    }

    return NULL;
}

static int madt_getcount(int type) {
    struct acpi_entry_hdr *entry = g_ioapic_list_start;
    int count = 0;

    while (entry < g_ioapic_list_end) {
        if (entry->type == type) count++;
        entry = madt_getnext(entry);
    }

    return count;
}

static void iored_write(void *ioapic, u8 entry, u8 vector, u8 delivery,
                        u8 destmode, u8 polarity, u8 mode, u8 mask, u8 dest) {
    u32 val = vector;
    val |= (delivery & 0b111) << 8;
    val |= (destmode & 1) << 11;
    val |= (polarity & 1) << 13;
    val |= (mode & 1) << 15;
    val |= (mask & 1) << 16;

    ioapic_write(ioapic, 0x10 + entry * 2, val);
    ioapic_write(ioapic, 0x11 + entry * 2, (u32)dest << 24);
}

int ioapic_set_irq(u32 irq, u8 vector, u8 proc, bool masked) {
    // default settings for ISA irqs
    u8 polarity = 0; // active high
    u8 trigger = 0;  // edge triggered

    for (usize i = 0; i < g_override_count; ++i) {
        struct acpi_madt_interrupt_source_override *override = madt_getentry(
            ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE, i);
        assert(override != NULL);

        if (override->source == irq) {
            polarity = override->flags & 2 ? 1 : 0; // active low
            trigger = override->flags & 8 ? 1 : 0;  // level triggered
            irq = override->gsi;
            break;
        }
    }

    struct IOAPICDevice *ioapic = ioapic_by_gsi(irq);
    if (ioapic == NULL) return EINVAL;
    irq = irq - ioapic->base;
    spinlock_acq(&g_ioapic_lock);
    iored_write(ioapic->addr, irq, vector, 0, 0, polarity, trigger,
                masked ? 1 : 0, proc);
    spinlock_rel(&g_ioapic_lock);
    return 0;
}

int ioapic_mask_irq(u32 irq) {
    for (usize i = 0; i < g_override_count; i++) {
        struct acpi_madt_interrupt_source_override *override = madt_getentry(
            ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE, i);
        if (override->source == irq) {
            irq = override->gsi;
            break;
        }
    }
    struct IOAPICDevice *ioapic = ioapic_by_gsi(irq);
    if (ioapic == NULL) return EINVAL;
    int reg = IOAPIC_REG_ENTRY + 2 * (irq - ioapic->base);
    spinlock_acq(&g_ioapic_lock);
    ioapic_write(ioapic->addr, reg,
                 ioapic_read(ioapic->addr, reg) | (1U << 16));
    spinlock_rel(&g_ioapic_lock);
    return 0;
}

void ioapic_init(void) {
    log("initializing ioapic");
    ACPITable tbl;
    int err = acpi_impl_find_table_by_signature(ACPI_MADT_SIGNATURE, &tbl);
    assert(err == 0);

    MADTTable = tbl.phys_addr;
    log_debug("found madt table at phys=%p virt=%p", tbl.phys_addr,
              tbl.virt_addr);
    g_ioapic_list_start = (void *)&MADTTable->entries;
    g_ioapic_list_end = (void *)((uptr)MADTTable + MADTTable->hdr.length);

    g_override_count = madt_getcount(
        ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE);
    g_ioapic_count = madt_getcount(ACPI_MADT_ENTRY_TYPE_IOAPIC);
    log_debug("found %zu overrides and %zu ioapics", g_override_count,
              g_ioapic_count);

    for (usize i = 0; i < g_ioapic_count; i++) {
        struct acpi_madt_ioapic *entry = madt_getentry(
            ACPI_MADT_ENTRY_TYPE_IOAPIC, i);
        struct IOAPICDevice *ioapic = &g_ioapic_array[i];

        ioapic->addr = vmm_map(NULL, NULL, (void *)(uptr)entry->address,
                               PAGE_SIZE, VMM_FLAGS_NOHINT | VMM_FLAGS_PHYSICAL,
                               MMU_FLAGS_READ | MMU_FLAGS_WRITE |
                                   MMU_FLAGS_NOEXEC);

        ioapic->base = entry->gsi_base;
        usize count = ioapic_read(ioapic->addr, IOAPIC_REG_ENTRY_COUNT);
        ioapic->top = entry->gsi_base + ((count >> 16) & 0xff) + 1;

        for (int j = ioapic->base; j < ioapic->top; j++) {
            iored_write(ioapic->addr, j - ioapic->base, 0xfe, 0, 0, 0, 0, 1, 0);
        }
    }
}
