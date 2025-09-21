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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

#ifndef HAVE_OPT_PCI
#error "cannot include this header without having opt/pci"
#endif

#define OPT_PCI_BAR_BASE 0x10

// 2 byte fields
#define OPT_PCI_CONFIG_VENDOR      0x0
#define OPT_PCI_CONFIG_DEVICEID    0x2
#define OPT_PCI_CONFIG_COMMAND     0x4
#define OPT_PCI_COMMAND_IO         0x1
#define OPT_PCI_COMMAND_MMIO       0x2
#define OPT_PCI_COMMAND_BUSMASTER  0x4
#define OPT_PCI_COMMAND_IRQDISABLE 0x400
#define OPT_PCI_CONFIG_STATUS      0x6
#define OPT_PCI_STATUS_HASCAP      0x10

// 1 byte fields
#define OPT_PCI_CONFIG_REVISION               0x8
#define OPT_PCI_CONFIG_PROGIF                 0x9
#define OPT_PCI_CONFIG_SUBCLASS               0xa
#define OPT_PCI_SUBCLASS_STORAGE_NVM          0x8
#define OPT_PCI_CONFIG_CLASS                  0xb
#define OPT_PCI_CLASS_STORAGE                 0x1
#define OPT_PCI_CONFIG_HEADERTYPE             0xe
#define OPT_PCI_HEADERTYPE_MULTIFUNCTION_MASK 0x80
#define OPT_PCI_HEADERTYPE_MASK               0x7f
#define OPT_PCI_HEADERTYPE_STANDARD           0
#define OPT_PCI_CONFIG_CAP                    0x34
#define OPT_PCI_CAP_MSI                       0x5
#define OPT_PCI_CAP_VENDORSPECIFIC            0x9
#define OPT_PCI_CAP_MSIX                      0x11

#define OPT_PCI_CAPOFF_ABSENT 0

typedef struct opt_pci_addr {
    uint32_t segment;
    uint32_t bus;
    uint32_t device;
    uint32_t function;
} opt_pci_addr_t;

typedef struct opt_pci_overrides {
    uint8_t (*read8)(const opt_pci_addr_t *addr, size_t off);
    uint16_t (*read16)(const opt_pci_addr_t *addr, size_t off);
    uint32_t (*read32)(const opt_pci_addr_t *addr, size_t off);
    void (*write8)(const opt_pci_addr_t *addr, size_t off, uint8_t val);
    void (*write16)(const opt_pci_addr_t *addr, size_t off, uint16_t val);
    void (*write32)(const opt_pci_addr_t *addr, size_t off, uint32_t val);
} opt_pci_overrides_t;

typedef struct {
    bool    exists;
    uint8_t offset;
} opt_pci_cap_t;

typedef struct {
    uintptr_t address;
    uintptr_t physical;
    size_t    length;
    bool      mmio;
    bool      is64bits;
    bool      prefetchable;
} opt_pci_bar_t;

typedef struct opt_pci_enum {
    LIST_ENTRY(opt_pci_enum) tqe_enums;
    opt_pci_addr_t addr;
    int            type;
    uint8_t        devclass;
    uint8_t        subclass;
    uint8_t        progif;
    uint16_t       vendor;
    uint16_t       deviceid;
    uint8_t        revision;
    opt_pci_cap_t  msi;
    opt_pci_cap_t  msix;
    opt_pci_bar_t  bar[6];
    union {
        struct {
            int       bir;
            uintmax_t tableoffset;
            int       pbir;
            uintmax_t pboffset;
            size_t    entrycount;
        } msix;
    } irq;
} opt_pci_enum_t;

// abstraction
int opt_pci_read8(uint8_t *out, const opt_pci_addr_t *addr, size_t off);
int opt_pci_read16(uint16_t *out, const opt_pci_addr_t *addr, size_t off);
int opt_pci_read32(uint32_t *out, const opt_pci_addr_t *addr, size_t off);
int opt_pci_write8(const opt_pci_addr_t *addr, size_t off, uint8_t val);
int opt_pci_write16(const opt_pci_addr_t *addr, size_t off, uint16_t val);
int opt_pci_write32(const opt_pci_addr_t *addr, size_t off, uint32_t val);

// query functions
uint8_t
opt_pci_get_capability_offset(opt_pci_enum_t *e, uint8_t cap, int max_loop);

// setup functions
void opt_pci_set_overrides(opt_pci_overrides_t *overrides);
void opt_pci_enumate(uint32_t start_bus, uint32_t end_bus);
