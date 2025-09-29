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

#include <arch/info.h>
#include <errno.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/opt/pci.h>
#include <kernel/platform/mmu.h>
#include <lib/util.h>
#include <stdbool.h>

// CREDIT: Mathewnd/Astral
// NOTE: mostly inspired by Astral, also uses some parts from 2022 SalernOS

#define PCI_LOG(fmt, ...) KOPTMSG("PCI", fmt "\n", __VA_ARGS__)
#define PCI_PANIC()       com_sys_panic(NULL, "pci error");

#define PCI_BAR_IO           1
#define PCI_BAR_TYPE_MASK    0x6
#define PCI_BAR_TYPE_64BIT   0x4
#define PCI_BAR_PREFETCHABLE 0x8

#define PCI_FUNC_EXISTS(bus, device, function) \
    (0xffffffff != pci_raw_read32(bus, device, function, 0))
#define PCI_DEV_EXISTS(bus, device) PCI_FUNC_EXISTS(bus, device, 0)

#define PCI_NUM_DEVICES_PER_BUS 32

#define PCI_FOREACH_ENUM_WILDCARD(currptr, wildcard, wildcard_mask) \
    for (*(currptr) = pci_enum_first(wildcard, wildcard_mask);      \
         NULL != *(currptr);                                        \
         *(currptr) = pci_enum_next(*(currptr), wildcard, wildcard_mask))

struct msix_message {
    uint32_t addresslow;
    uint32_t addresshigh;
    uint32_t data;
    uint32_t control;
} __attribute__((packed));

static opt_pci_overrides_t *Overrides = NULL;
static LIST_HEAD(, opt_pci_enum) PCIEnumeratorList;

static inline bool pci_wildcard_match(opt_pci_enum_t *e,
                                      opt_pci_query_t wildcard,
                                      int             wildcard_mask) {
    bool class_matches = !(OPT_PCI_QUERYMASK_CLASS & wildcard_mask) ||
                         e->info.clazz == wildcard.clazz;
    bool subclass_matches = !(OPT_PCI_QUERYMASK_SUBCLASS & wildcard_mask) ||
                            e->info.subclass == wildcard.subclass;
    bool progif_matches = !(OPT_PCI_QUERYMASK_PROGIF & wildcard_mask) ||
                          e->info.progif == wildcard.progif;
    bool vendor_matches = !(OPT_PCI_QUERYMASK_VENDOR & wildcard_mask) ||
                          e->info.vendor == wildcard.vendor;
    bool deviceid_matches = !(OPT_PCI_QUERYMASK_DEVICEID & wildcard_mask) ||
                            e->info.deviceid == wildcard.deviceid;
    bool revision_matches = !(OPT_PCI_QUERYMASK_REVISION & wildcard_mask) ||
                            e->info.revision == wildcard.revision;

    return class_matches && subclass_matches && progif_matches &&
           vendor_matches && deviceid_matches && revision_matches;
}

static inline opt_pci_enum_t *pci_enum_first(opt_pci_query_t wildcard,
                                             int             wildcard_mask) {
    opt_pci_enum_t *e, *_;
    LIST_FOREACH_SAFE(e, &PCIEnumeratorList, tqe_enums, _) {
        if (pci_wildcard_match(e, wildcard, wildcard_mask)) {
            return e;
        }
    }

    return NULL;
}

static inline opt_pci_enum_t *
pci_enum_next(opt_pci_enum_t *e, opt_pci_query_t wildcard, int wildcard_mask) {
    for (opt_pci_enum_t *next = LIST_NEXT(e, tqe_enums); NULL != next;
         next                 = LIST_NEXT(next, tqe_enums)) {
        if (pci_wildcard_match(next, wildcard, wildcard_mask)) {
            return next;
        }
    }

    return NULL;
}

static void *pci_map_bar(opt_pci_bar_t bar) {
    arch_mmu_flags_t cache_flags = (bar.prefetchable ? ARCH_MMU_FLAGS_WT
                                                     : ARCH_MMU_FLAGS_UC);
    return com_mm_vmm_map(NULL,
                          NULL,
                          (void *)bar.address,
                          bar.length,
                          COM_MM_VMM_FLAGS_NOHINT | COM_MM_VMM_FLAGS_PHYSICAL,
                          ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                              ARCH_MMU_FLAGS_NOEXEC | cache_flags);
}

static inline uint8_t
pci_raw_read8(uint32_t bus, uint32_t dev, uint32_t func, size_t off) {
    opt_pci_addr_t addr = {.bus      = bus,
                           .device   = dev,
                           .function = func,
                           .segment  = 0};
    return opt_pci_read8(&addr, off);
}

static inline uint32_t
pci_raw_read32(uint32_t bus, uint32_t dev, uint32_t func, size_t off) {
    opt_pci_addr_t addr = {.bus      = bus,
                           .device   = dev,
                           .function = func,
                           .segment  = 0};
    return opt_pci_read32(&addr, off);
}

static void enumerate_function(uint32_t bus, uint32_t dev, uint32_t func) {
    opt_pci_enum_t *e = com_mm_slab_alloc(sizeof(*e));
    e->addr.bus       = bus;
    e->addr.device    = dev;
    e->addr.function  = func;
    e->addr.segment   = 0;
    e->info.clazz     = OPT_PCI_ENUM_READ8(e, OPT_PCI_CONFIG_CLASS);
    e->info.subclass  = OPT_PCI_ENUM_READ8(e, OPT_PCI_CONFIG_SUBCLASS);
    e->info.progif    = OPT_PCI_ENUM_READ8(e, OPT_PCI_CONFIG_PROGIF);
    e->info.vendor    = OPT_PCI_ENUM_READ16(e, OPT_PCI_CONFIG_VENDOR);
    e->info.deviceid  = OPT_PCI_ENUM_READ16(e, OPT_PCI_CONFIG_DEVICEID);
    e->info.revision  = OPT_PCI_ENUM_READ8(e, OPT_PCI_CONFIG_REVISION);

    uint8_t msix_off = opt_pci_get_capability_offset(e, OPT_PCI_CAP_MSIX, 0);
    if (OPT_PCI_CAPOFF_ABSENT != msix_off) {
        e->msix.exists = true;
        e->msix.offset = msix_off;
    }

    uint8_t msi_off = opt_pci_get_capability_offset(e, OPT_PCI_CAP_MSI, 0);
    if (OPT_PCI_CAPOFF_ABSENT != msi_off) {
        e->msi.exists = true;
        e->msi.offset = msi_off;
    }

    LIST_INSERT_HEAD(&PCIEnumeratorList, e, tqe_enums);

    PCI_LOG("(info) device: %x:%x.%x %x %x %x:%x (rev = %x) (interrupt = %s)",
            bus,
            dev,
            func,
            e->info.clazz,
            e->info.subclass,
            e->info.vendor,
            e->info.deviceid,
            e->info.revision,
            (e->msix.exists)  ? "msix"
            : (e->msi.exists) ? "msi"
                              : "n/a");
}

static void enumerate_device(uint32_t bus, uint32_t dev) {
    bool is_multifunction = pci_raw_read8(bus,
                                          dev,
                                          0,
                                          OPT_PCI_CONFIG_HEADERTYPE) &
                            OPT_PCI_HEADERTYPE_MULTIFUNCTION_MASK;
    uint32_t num_functions = (is_multifunction) ? 8 : 1;

    for (uint32_t func = 0; func < num_functions; func++) {
        if (PCI_FUNC_EXISTS(bus, dev, func)) {
            enumerate_function(bus, dev, func);
        }
    }
}

// abstraction functions

uint8_t opt_pci_read8(const opt_pci_addr_t *addr, size_t off) {
    KASSERT(NULL != Overrides);
    return Overrides->read8(addr, off);
}

uint16_t opt_pci_read16(const opt_pci_addr_t *addr, size_t off) {
    KASSERT(NULL != Overrides);
    return Overrides->read16(addr, off);
}

uint32_t opt_pci_read32(const opt_pci_addr_t *addr, size_t off) {
    KASSERT(NULL != Overrides);
    return Overrides->read32(addr, off);
}

void opt_pci_write8(const opt_pci_addr_t *addr, size_t off, uint8_t val) {
    KASSERT(NULL != Overrides);
    Overrides->write8(addr, off, val);
}

void opt_pci_write16(const opt_pci_addr_t *addr, size_t off, uint16_t val) {
    KASSERT(NULL != Overrides);
    Overrides->write16(addr, off, val);
}

void opt_pci_write32(const opt_pci_addr_t *addr, size_t off, uint32_t val) {
    KASSERT(NULL != Overrides);
    Overrides->write32(addr, off, val);
}

// PCI interface

uint8_t
opt_pci_get_capability_offset(opt_pci_enum_t *e, uint8_t cap, int max_loop) {
    if (!(OPT_PCI_STATUS_HASCAP &
          OPT_PCI_ENUM_READ16(e, OPT_PCI_CONFIG_STATUS))) {
        return 0;
    }

    uint8_t offset = OPT_PCI_ENUM_READ8(e, OPT_PCI_CONFIG_CAP);
    while (0 != offset) {
        if (cap == OPT_PCI_ENUM_READ8(e, offset) && 0 == max_loop--) {
            break;
        }
        offset = OPT_PCI_ENUM_READ8(e, offset + 1);
    }

    return offset;
}

void opt_pci_set_command(opt_pci_enum_t *e, uint16_t mask, int mask_mode) {
    uint16_t command = OPT_PCI_ENUM_READ16(e, OPT_PCI_CONFIG_COMMAND);

    switch (mask_mode) {
        case OPT_PCI_MASKMODE_SET:
            command |= mask;
            break;
        case OPT_PCI_MASKMODE_UNSET:
            command &= ~mask;
            break;
        case OPT_PCI_MASKMODE_OVERRIDE:
            command = mask;
            break;
        default:
            PCI_LOG("(error) unknown mask mode %d", mask_mode);
            PCI_PANIC();
    }

    OPT_PCI_ENUM_WRITE16(e, OPT_PCI_CONFIG_COMMAND, command);
}

opt_pci_bar_t opt_pci_get_bar(opt_pci_enum_t *e, size_t index) {
    if (0 != e->bar[index].length) {
        return e->bar[index];
    }

    opt_pci_bar_t ret;
    size_t        off = 0x10 + index * 4;
    uint32_t      bar = OPT_PCI_ENUM_READ32(e, off);

    if (PCI_BAR_IO & bar) {
        ret.mmio         = false;
        ret.prefetchable = false;
        ret.is64bits     = false;
        ret.address      = bar & 0xfffffffc;
        OPT_PCI_ENUM_WRITE32(e, off, 0xffffffff);
        ret.length = -1;
        OPT_PCI_ENUM_WRITE32(e, off, bar);
        goto end;
    }

    ret.mmio         = true;
    ret.prefetchable = PCI_BAR_PREFETCHABLE & bar;
    ret.address = ret.physical = bar & 0xfffffff0;
    ret.length                 = -1;
    ret.is64bits = PCI_BAR_TYPE_64BIT == (bar & PCI_BAR_TYPE_MASK);

    if (ret.is64bits) {
        ret.address |= (uint64_t)OPT_PCI_ENUM_READ32(e, off + 4) << 32;
    }

    OPT_PCI_ENUM_WRITE32(e, off, 0xffffffff);
    ret.length = ~(OPT_PCI_ENUM_READ32(e, off) & 0xfffffff0) + 1;
    OPT_PCI_ENUM_WRITE32(e, off, bar);
    ret.address = (uintptr_t)pci_map_bar(ret);

end:
    e->bar[index] = ret;
    return ret;
}

void opt_pci_msix_set_mask(opt_pci_enum_t *e, int mask_mode) {
    uint16_t msgctl = OPT_PCI_ENUM_READ16(e, e->msix.offset + 2);

    switch (mask_mode) {
        case OPT_PCI_MASKMODE_SET:
            msgctl |= 0x4000;
            break;
        case OPT_PCI_MASKMODE_UNSET:
            msgctl &= ~0x4000;
            break;
        default:
            PCI_LOG("(error) unknown mask mode %d", mask_mode);
            PCI_PANIC();
    }

    OPT_PCI_ENUM_WRITE16(e, e->msix.offset + 2, msgctl);
}

void opt_pci_msix_add(opt_pci_enum_t *e,
                      uint8_t         msixvec,
                      uintmax_t       intvec,
                      int             flags) {
    KASSERT(e->msix.exists);
    KASSERT(msixvec < e->irq.msix.num_entries);

    // NOTE: here address is 64 bits because we then have to split it into low
    // and high chunks, but this is still portable as long as it is not used on
    // a machine with addresses larger than 64 bits. But I think this is
    // something the PCI people will look into, not me.
    opt_pci_bar_t bir = opt_pci_get_bar(e, e->irq.msix.bir);
    volatile struct msix_message
        *msix_table = (void *)(bir.address + e->irq.msix.table_offset);
    volatile struct msix_message *msix_entry = &msix_table[msixvec];

    uint32_t data;
    uint64_t address = arch_pci_msi_format_message(&data, intvec, flags);

    msix_entry->addresslow  = address & 0xffffffff;
    msix_entry->addresshigh = (address >> 32) & 0xffffffff;
    msix_entry->data        = data;
    msix_entry->control     = 0;
}

uint16_t opt_pci_msix_init(opt_pci_enum_t *e) {
    if (!e->msix.exists) {
        return 0;
    }

    uint16_t msgcntl = OPT_PCI_ENUM_READ16(e, e->msix.offset + 2);
    msgcntl |= 0xC000;
    OPT_PCI_ENUM_WRITE16(e, e->msix.offset + 2, msgcntl);
    e->irq.msix.num_entries = (msgcntl & 0x3ff) + 1;

    uint32_t bir             = OPT_PCI_ENUM_READ32(e, e->msix.offset + 4);
    e->irq.msix.bir          = bir & 0b111;
    e->irq.msix.table_offset = bir & ~(uint32_t)0b111;

    uint32_t pbir          = OPT_PCI_ENUM_READ32(e, e->msix.offset + 8);
    e->irq.msix.pbir       = pbir & 0b111;
    e->irq.msix.pba_offset = pbir & ~(uint32_t)0b111;

    opt_pci_set_command(e, OPT_PCI_COMMAND_IRQDISABLE, OPT_PCI_MASKMODE_SET);

    return e->irq.msix.num_entries;
}

// driver interface

int opt_pci_install_driver(opt_pci_dev_driver_t *driver) {
    opt_pci_enum_t *e;
    int             ret = ENODEV;

    PCI_FOREACH_ENUM_WILDCARD(&e, driver->wildcard, driver->wildcard_mask) {
        driver->init_dev(e);
        ret = 0;
    }

    return ret;
}

// setup functions

void opt_pci_set_overrides(opt_pci_overrides_t *overrides) {
    KASSERT(NULL == Overrides);
    Overrides = overrides;
    LIST_INIT(&PCIEnumeratorList);
}

void opt_pci_enumate(uint32_t start_bus, uint32_t end_bus) {
    KLOG("enumerating pci");

    for (uint32_t bus = start_bus; bus < end_bus; bus++) {
        for (uint32_t dev = 0; dev < PCI_NUM_DEVICES_PER_BUS; dev++) {
            if (PCI_DEV_EXISTS(bus, dev)) {
                enumerate_device(bus, dev);
            }
        }
    }
}
