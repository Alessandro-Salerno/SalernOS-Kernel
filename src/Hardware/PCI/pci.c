/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2022 Alessandro Salerno

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**********************************************************************/

#include "Hardware/PCI/pci.h"
#include "Hardware/Drivers/AHCI/ahci.h"
#include "Memory/paging.h"
#include "kernelpanic.h"
#include <kmalloc.h>
#include <kstdio.h>

#define DEV_PER_BUS 32
#define FN_PER_DEV  8

// Device Classes
#define MASS_STORAGE_CONTROLLER 0x01

// Device Subclasses
#define SERIAL_ATA 0x06

// Program interfaces
#define AHCI_1_0 0x01

static void __enumerate_function__(uint64_t __devaddr, uint64_t __function) {
  int64_t  _fn_offset = __function << 12;
  uint64_t _fn_addr   = __devaddr + _fn_offset;

  kernel_paging_address_map((void *)(_fn_addr), (void *)(_fn_addr));

  pcidevhdr_t *_pci_dev = (pcidevhdr_t *)(_fn_addr);
  SOFTASSERT(_pci_dev->_Device, RETVOID);
  SOFTASSERT(_pci_dev->_Device != 0xffff, RETVOID);

  // Check device class
  switch (_pci_dev->_Class) {
    case MASS_STORAGE_CONTROLLER: {
      // Check device Subclass
      switch (_pci_dev->_Subclass) {
        case SERIAL_ATA: {
          // Check device program interface
          switch (_pci_dev->_ProgramInterface) {
            case AHCI_1_0: {
              // Create driver instance
              ahcidevdr_t *_driver    = kmalloc(sizeof(ahcidevdr_t));
              _driver->_PCIDeviceBase = _pci_dev;
              _driver->_ABAR =
                  (hbamem_t *)((uint64_t)(((pcidevhdr0_t *)(_pci_dev))->_BAR5));

              kernel_paging_address_map(_driver->_ABAR, _driver->_ABAR);
              kernel_hw_ahci_ports_probe(_driver);
            }
          }
        }
      }
    }
  }
}

static void __enumerate_dev__(uint64_t __busaddr, uint64_t __dev) {
  int64_t  _dev_offset = __dev << 15;
  uint64_t _dev_addr   = __busaddr + _dev_offset;

  kernel_paging_address_map((void *)(_dev_addr), (void *)(_dev_addr));

  pcidevhdr_t *_pci_dev = (pcidevhdr_t *)(_dev_addr);
  SOFTASSERT(_pci_dev->_Device, RETVOID);
  SOFTASSERT(_pci_dev->_Device != 0xffff, RETVOID);

  for (uint64_t _fn = 0; _fn < FN_PER_DEV; _fn++) {
    __enumerate_function__(_dev_addr, _fn);
  }
}

static void __enumerate_bus__(uint64_t __baseaddr, uint64_t __bus) {
  uint64_t _bus_offset = __bus << 20;
  uint64_t _bus_addr   = __baseaddr + _bus_offset;

  kernel_paging_address_map((void *)(_bus_addr), (void *)(_bus_addr));

  pcidevhdr_t *_pci_dev = (pcidevhdr_t *)(_bus_addr);
  SOFTASSERT(_pci_dev->_Device, RETVOID);
  SOFTASSERT(_pci_dev->_Device != 0xffff, RETVOID);

  for (uint64_t _dev = 0; _dev < DEV_PER_BUS; _dev++) {
    __enumerate_dev__(_bus_addr, _dev);
  }
}

void kernel_hw_pci_enumerate(mcfghdr_t *__mcfgtable) {
  uint64_t _nentries = (__mcfgtable->_SDTHeader._Length - sizeof(mcfghdr_t)) /
                       sizeof(acpidevconf_t);

  for (uint64_t _i = 0; _i < _nentries; _i++) {
    acpidevconf_t *_dev_config =
        (acpidevconf_t *)((uint64_t)(__mcfgtable) + sizeof(mcfghdr_t) +
                          (sizeof(acpidevconf_t) * _i));

    for (uint64_t _bus = _dev_config->_StartBus; _bus < _dev_config->_EndBus;
         _bus++) {
      __enumerate_bus__(_dev_config->_BaseAddress, _bus);
    }
  }
}
