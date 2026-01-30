/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
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

#include <kernel/com/mm/vmm.h>
#include <kernel/opt/acpi.h>
#include <kernel/opt/uacpi.h>
#include <kernel/platform/mmu.h>
#include <lib/util.h>
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

struct ioapic {
    void *addr;
    int   base;
    int   top;
};

static struct acpi_madt      *MADTTable;
static struct ioapic          IOAPICArray[MAX_IOAPICS];
static struct acpi_entry_hdr *IOAPICListStart;
static struct acpi_entry_hdr *IOAPICListEnd;

static size_t OverrideCount;
static size_t IOAPICCount;

static inline uint32_t ioapic_read(void *ioapic, int reg) {
    volatile uint32_t *apic = ioapic;
    *apic                   = reg & 0xff;
    return *(apic + 4);
}

static inline void ioapic_write(void *ioapic, int reg, uint32_t v) {
    volatile uint32_t *apic = ioapic;
    *apic                   = reg & 0xff;
    *(apic + 4)             = v;
}

static struct ioapic *ioapic_by_gsi(int gsi) {
    for (size_t i = 0; i < IOAPICCount; ++i) {
        struct ioapic *ioapic = &IOAPICArray[i];
        if (gsi >= ioapic->base && gsi < ioapic->top) {
            return ioapic;
        }
    }

    return NULL;
}

static inline struct acpi_entry_hdr *
madt_getnext(struct acpi_entry_hdr *header) {
    uintptr_t ptr = (uintptr_t)header;
    return (struct acpi_entry_hdr *)(ptr + header->length);
}

static inline void *madt_getentry(int type, size_t n) {
    struct acpi_entry_hdr *entry = IOAPICListStart;

    while (entry < IOAPICListEnd) {
        if (entry->type == type) {
            if (0 == n--) {
                return entry;
            }
        }
        entry = madt_getnext(entry);
    }

    return NULL;
}

static int madt_getcount(int type) {
    struct acpi_entry_hdr *entry = IOAPICListStart;
    int                    count = 0;

    while (entry < IOAPICListEnd) {
        if (entry->type == type) {
            count++;
        }
        entry = madt_getnext(entry);
    }

    return count;
}

static void iored_write(void   *ioapic,
                        uint8_t entry,
                        uint8_t vector,
                        uint8_t delivery,
                        uint8_t destmode,
                        uint8_t polarity,
                        uint8_t mode,
                        uint8_t mask,
                        uint8_t dest) {
    uint32_t val = vector;
    val |= (delivery & 0b111) << 8;
    val |= (destmode & 1) << 11;
    val |= (polarity & 1) << 13;
    val |= (mode & 1) << 15;
    val |= (mask & 1) << 16;

    ioapic_write(ioapic, 0x10 + entry * 2, val);
    ioapic_write(ioapic, 0x11 + entry * 2, (uint32_t)dest << 24);
}

void x86_64_ioapic_set_irq(uint8_t irq,
                           uint8_t vector,
                           uint8_t proc,
                           bool    masked) {
    // default settings for ISA irqs
    uint8_t polarity = 1; // active high
    uint8_t trigger  = 0; // edge triggered

    for (size_t i = 0; i < OverrideCount; ++i) {
        struct acpi_madt_interrupt_source_override *override = madt_getentry(
            ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE,
            i);
        KASSERT(NULL != override);

        if (override->source == irq) {
            polarity = override->flags & 2 ? 1 : 0; // active low
            trigger  = override->flags & 8 ? 1 : 0; // level triggered
            irq      = override->gsi;
            break;
        }
    }

    struct ioapic *ioapic = ioapic_by_gsi(irq);
    KASSERT(NULL != ioapic);
    irq = irq - ioapic->base;
    iored_write(ioapic->addr,
                irq,
                vector,
                0,
                0,
                polarity,
                trigger,
                masked ? 1 : 0,
                proc);
}

void x86_64_ioapic_init(void) {
    KLOG("initializing ioapic");
    opt_acpi_table_t tbl;
    int err = opt_acpi_impl_find_table_by_signature(&tbl, ACPI_MADT_SIGNATURE);
    KASSERT(0 == err);

    MADTTable = tbl.phys_addr;
    KDEBUG("found madt table at phys=%p virt=%p", tbl.phys_addr, tbl.virt_addr);
    IOAPICListStart = (void *)&MADTTable->entries;
    IOAPICListEnd   = (void *)((uintptr_t)MADTTable + MADTTable->hdr.length);

    OverrideCount = madt_getcount(
        ACPI_MADT_ENTRY_TYPE_INTERRUPT_SOURCE_OVERRIDE);
    IOAPICCount = madt_getcount(ACPI_MADT_ENTRY_TYPE_IOAPIC);
    KDEBUG("found %zu overrides and %zu ioapics", OverrideCount, IOAPICCount);

    for (size_t i = 0; i < IOAPICCount; i++) {
        struct acpi_madt_ioapic *entry = madt_getentry(
            ACPI_MADT_ENTRY_TYPE_IOAPIC,
            i);
        struct ioapic *ioapic = &IOAPICArray[i];

        ioapic->addr = com_mm_vmm_map(
            NULL,
            NULL,
            (void *)(uintptr_t)entry->address,
            ARCH_PAGE_SIZE,
            COM_MM_VMM_FLAGS_NOHINT | COM_MM_VMM_FLAGS_PHYSICAL,
            ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC);

        ioapic->base = entry->gsi_base;
        size_t count = ioapic_read(ioapic->addr, IOAPIC_REG_ENTRY_COUNT);
        ioapic->top  = entry->gsi_base + ((count >> 16) & 0xff) + 1;

        for (int j = ioapic->base; j < ioapic->top; j++) {
            iored_write(ioapic->addr, j - ioapic->base, 0xfe, 0, 0, 0, 0, 1, 0);
        }
    }
}
