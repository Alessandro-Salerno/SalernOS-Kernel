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

#include <kernel/com/mm/slab.h>
#include <kernel/opt/pci.h>
#include <lib/util.h>
#include <stdbool.h>

// CREDIT: Mathewnd/Astral
// NOTE: mostly inspired by Astral, also uses some parts from 2022 SalernOS

#define PCI_FUNC_EXISTS(bus, device, function) \
    (0xffffffff != pci_raw_read32(bus, device, function, 0))
#define PCI_DEV_EXISTS(bus, device) PCI_FUNC_EXISTS(bus, device, 0)

#define PCI_NUM_DEVICES_PER_BUS 32

struct msix_message {
    uint32_t addresslow;
    uint32_t addresshigh;
    uint32_t data;
    uint32_t control;
} __attribute__((packed));

static opt_pci_overrides_t *Overrides = NULL;
static LIST_HEAD(, opt_pci_enum) PCIEnumeratorList;

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
    e->devclass       = OPT_PCI_ENUM_READ8(e, OPT_PCI_CONFIG_CLASS);
    e->subclass       = OPT_PCI_ENUM_READ8(e, OPT_PCI_CONFIG_SUBCLASS);
    e->progif         = OPT_PCI_ENUM_READ8(e, OPT_PCI_CONFIG_PROGIF);
    e->vendor         = OPT_PCI_ENUM_READ16(e, OPT_PCI_CONFIG_VENDOR);
    e->deviceid       = OPT_PCI_ENUM_READ16(e, OPT_PCI_CONFIG_DEVICEID);
    e->revision       = OPT_PCI_ENUM_READ8(e, OPT_PCI_CONFIG_REVISION);

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

    KDEBUG("pci device %x:%x.%x %x %x %x:%x (rev = %x) (interrupt = %s)",
           bus,
           dev,
           func,
           e->devclass,
           e->subclass,
           e->vendor,
           e->deviceid,
           e->revision,
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
    Overrides->write8(addr, off, val);
}

void opt_pci_write32(const opt_pci_addr_t *addr, size_t off, uint32_t val) {
    KASSERT(NULL != Overrides);
    Overrides->write8(addr, off, val);
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

uint16_t opt_pci_init_msix(opt_pci_enum_t *e) {
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

    return e->irq.msix.num_entries;
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
