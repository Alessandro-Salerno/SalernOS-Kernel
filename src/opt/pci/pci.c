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

#define PCI_FUNC_EXISTS(bus, device, function) \
    (0xffffffff != pci_direct_read32(bus, device, function, 0))
#define PCI_DEV_EXISTS(bus, device) PCI_FUNC_EXISTS(bus, device, 0)

#define __PCI_ENUM_CALL(func, e, off) \
    func((e)->addr.bus, (e)->addr.device, (e)->addr.function, off)
#define PCI_ENUM_READ8(e, off)  __PCI_ENUM_CALL(pci_direct_read8, e, off)
#define PCI_ENUM_READ16(e, off) __PCI_ENUM_CALL(pci_direct_read16, e, off)
#define PCI_ENUM_READ32(e, off) __PCI_ENUM_CALL(pci_direct_read32, e, off)

#define PCI_NUM_DEVICES_PER_BUS 32

static opt_pci_overrides_t *Overrides = NULL;
static LIST_HEAD(, opt_pci_enum) PCIEnumeratorList;

static inline uint32_t
pci_direct_read32(uint32_t bus, uint32_t dev, uint32_t func, size_t off) {
    uint32_t       ret;
    opt_pci_addr_t addr = {.bus = bus, .device = dev, .function = func};
    opt_pci_read32(&ret, &addr, off);
    return ret;
}

static inline uint16_t
pci_direct_read16(uint32_t bus, uint32_t dev, uint32_t func, size_t off) {
    uint16_t       ret;
    opt_pci_addr_t addr = {.bus = bus, .device = dev, .function = func};
    opt_pci_read16(&ret, &addr, off);
    return ret;
}

static inline uint8_t
pci_direct_read8(uint32_t bus, uint32_t dev, uint32_t func, size_t off) {
    uint8_t        ret;
    opt_pci_addr_t addr = {.bus = bus, .device = dev, .function = func};
    opt_pci_read8(&ret, &addr, off);
    return ret;
}

static void enumerate_function(uint32_t bus, uint32_t dev, uint32_t func) {
    opt_pci_enum_t *e = com_mm_slab_alloc(sizeof(*e));
    e->addr.bus       = bus;
    e->addr.device    = dev;
    e->addr.function  = func;
    e->addr.segment   = 0;
    opt_pci_read8(&e->devclass, &e->addr, OPT_PCI_CONFIG_CLASS);
    opt_pci_read8(&e->subclass, &e->addr, OPT_PCI_CONFIG_SUBCLASS);
    opt_pci_read8(&e->progif, &e->addr, OPT_PCI_CONFIG_PROGIF);
    opt_pci_read16(&e->vendor, &e->addr, OPT_PCI_CONFIG_VENDOR);
    opt_pci_read16(&e->deviceid, &e->addr, OPT_PCI_CONFIG_DEVICEID);
    opt_pci_read8(&e->revision, &e->addr, OPT_PCI_CONFIG_REVISION);

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
    bool is_multifunction = pci_direct_read8(bus,
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

int opt_pci_read8(uint8_t *out, const opt_pci_addr_t *addr, size_t off) {
    KASSERT(NULL != Overrides);
    *out = Overrides->read8(addr, off);
    return 0;
}

int opt_pci_read16(uint16_t *out, const opt_pci_addr_t *addr, size_t off) {
    KASSERT(NULL != Overrides);
    *out = Overrides->read16(addr, off);
    return 0;
}

int opt_pci_read32(uint32_t *out, const opt_pci_addr_t *addr, size_t off) {
    KASSERT(NULL != Overrides);
    *out = Overrides->read32(addr, off);
    return 0;
}

int opt_pci_write8(const opt_pci_addr_t *addr, size_t off, uint8_t val) {
    KASSERT(NULL != Overrides);
    Overrides->write8(addr, off, val);
    return 0;
}

int opt_pci_write16(const opt_pci_addr_t *addr, size_t off, uint16_t val) {
    KASSERT(NULL != Overrides);
    Overrides->write8(addr, off, val);
    return 0;
}

int opt_pci_write32(const opt_pci_addr_t *addr, size_t off, uint32_t val) {
    KASSERT(NULL != Overrides);
    Overrides->write8(addr, off, val);
    return 0;
}

uint8_t
opt_pci_get_capability_offset(opt_pci_enum_t *e, uint8_t cap, int max_loop) {
    if (!(OPT_PCI_STATUS_HASCAP & PCI_ENUM_READ16(e, OPT_PCI_CONFIG_STATUS))) {
        return 0;
    }

    uint8_t offset = PCI_ENUM_READ8(e, OPT_PCI_CONFIG_CAP);
    while (0 != offset) {
        if (cap == PCI_ENUM_READ8(e, offset) && 0 == max_loop--) {
            break;
        }
        offset = PCI_ENUM_READ8(e, offset + 1);
    }

    return offset;
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
