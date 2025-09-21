/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#include <arch/mmu.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/opt/acpi.h>
#include <kernel/opt/pci.h>
#include <kernel/platform/mmu.h>
#include <lib/mem.h>
#include <lib/util.h>
#include <limits.h>
#include <vendor/acpi.h>

// TAKEN: Mathewnd/Astral
#define MCFG_MAPPING_SIZE(buscount) (4096UL * 8UL * 32UL * (buscount))

static struct acpi_mcfg_allocation *MCFGEntries    = NULL;
static size_t                       MCFGNumEntries = 0;

// TODO: figure if this is used frequently and if so, use radixttree for
// constant time lookups
static inline struct acpi_mcfg_allocation *mcfg_getentry(int bus) {
    for (size_t i = 0; i < MCFGNumEntries; i++) {
        struct acpi_mcfg_allocation *entry = &MCFGEntries[i];
        if (bus >= entry->start_bus && bus <= entry->end_bus) {
            return entry;
        }
    }

    return NULL;
}

// NOTE: this returns the address after aligning the offset to the lower 4-byte
// multiple. So to read at unaligned offsets, additional logic is needed. All
// these alignment things are very weird, there may be some requirement when
// accessing MCFG that either I'm not aware of, or is only applicable to
// architectures other than x86-64
static inline volatile void *mcfg_getaddr(const opt_pci_addr_t *addr,
                                          size_t                off) {
    struct acpi_mcfg_allocation *entry = mcfg_getentry(addr->bus);
    if (NULL == entry) {
        return NULL;
    }

    uintptr_t base = (uintptr_t)entry->address;
    return (volatile void *)((uintptr_t)base +
                             (((addr->bus - entry->start_bus) << 20) |
                              (addr->device << 15) | (addr->function << 12) |
                              (off & ~0x3)));
}

static uint32_t mcfg_read32_at(volatile uint32_t *address, size_t off) {
    uint32_t full_value   = *address;
    size_t   internal_off = (off & 0b11) * CHAR_BIT;
    return full_value >> internal_off;
}

static uint32_t mcfg_read32(const opt_pci_addr_t *addr, size_t off) {
    volatile uint32_t *address = mcfg_getaddr(addr, off);
    if (NULL == address) {
        return 0xffffffff;
    }
    return mcfg_read32_at(address, off);
}

static uint16_t mcfg_read16(const opt_pci_addr_t *addr, size_t off) {
    uint32_t word = mcfg_read32(addr, off);
    return (uint16_t)(word & 0xffff);
}

static uint8_t mcfg_read8(const opt_pci_addr_t *addr, size_t off) {
    uint32_t word = mcfg_read32(addr, off);
    return (uint8_t)(word & 0xff);
}

static void mcfg_write32(const opt_pci_addr_t *addr, size_t off, uint32_t val) {
    volatile uint32_t *address = mcfg_getaddr(addr, off);
    if (NULL == address) {
        return;
    }
    *address = val;
}

static void mcfg_write16(const opt_pci_addr_t *addr, size_t off, uint16_t val) {
    volatile uint32_t *address = mcfg_getaddr(addr, off);
    if (NULL == address) {
        return;
    }

    uint32_t old          = *address;
    size_t   internal_off = (off & 0b11) * CHAR_BIT;
    old &= ~(0xffffUL << internal_off);
    *address = old | (val << internal_off);
}

static void mcfg_write8(const opt_pci_addr_t *addr, size_t off, uint8_t val) {
    volatile uint8_t *address = mcfg_getaddr(addr, off);
    if (NULL != address) {
        *address = val;
    }

    uint32_t old          = *address;
    size_t   internal_off = (off & 0b11) * CHAR_BIT;
    old &= ~(0xffUL << internal_off);
    *address = old | (val << internal_off);
}

static opt_pci_overrides_t MCFGOverrides = {.read8   = mcfg_read8,
                                            .read16  = mcfg_read16,
                                            .read32  = mcfg_read32,
                                            .write8  = mcfg_write8,
                                            .write16 = mcfg_write16,
                                            .write32 = mcfg_write32};

int opt_acpi_init_pci(void) {
    KLOG("initializing mcfg");

    opt_acpi_table_t tbl;
    int ret = opt_acpi_impl_find_table_by_signature(&tbl, ACPI_MCFG_SIGNATURE);
    if (0 != ret) {
        goto end;
    }

    KDEBUG("found mcfg table at phys=%p, virt = %p",
           tbl.phys_addr,
           tbl.virt_addr);

    struct acpi_mcfg *mcfg = tbl.phys_addr;
    MCFGNumEntries         = (mcfg->hdr.length - sizeof(struct acpi_sdt_hdr)) /
                     sizeof(struct acpi_mcfg_allocation);

    void *entries_phys = com_mm_pmm_alloc_many(
        MCFGNumEntries * sizeof(struct acpi_mcfg_allocation) / ARCH_PAGE_SIZE +
        1);
    MCFGEntries = (void *)ARCH_PHYS_TO_HHDM(entries_phys);
    kmemcpy(MCFGEntries,
            mcfg->entries,
            MCFGNumEntries * sizeof(struct acpi_mcfg_allocation));

    arch_mmu_pagetable_t *pt = arch_mmu_get_table();

    for (size_t i = 0; i < MCFGNumEntries; i++) {
        void     *phys_addr    = (void *)MCFGEntries[i].address;
        void     *virt_addr    = (void *)ARCH_PHYS_TO_HHDM(phys_addr);
        uintptr_t page_off     = (uintptr_t)phys_addr % ARCH_PAGE_SIZE;
        size_t    mapping_size = page_off +
                              MCFG_MAPPING_SIZE(MCFGEntries[i].end_bus -
                                                MCFGEntries[i].start_bus + 1);
        size_t mapping_size_pages = (mapping_size + ARCH_PAGE_SIZE - 1) /
                                    ARCH_PAGE_SIZE;

        for (size_t j = 0; j < mapping_size_pages; j++) {
            uintptr_t page_to_off = j * ARCH_PAGE_SIZE;
            arch_mmu_map(pt,
                         (void *)((uintptr_t)virt_addr + page_to_off),
                         (void *)((uintptr_t)phys_addr + page_to_off),
                         ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_READ |
                             ARCH_MMU_FLAGS_NOEXEC);
        }

        MCFGEntries[i].address = (uintptr_t)virt_addr + page_off;
    }

    // TODO: unref the table
    opt_pci_set_overrides(&MCFGOverrides);

    for (size_t i = 0; i < MCFGNumEntries; i++) {
        struct acpi_mcfg_allocation *entry = &MCFGEntries[i];
        opt_pci_enumate(entry->start_bus, entry->end_bus);
    }

end:
    return ret;
}
