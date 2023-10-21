/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2023 Alessandro Salerno

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

#ifndef SALERNOS_CORE_KERNEL_HW_PCI
#define SALERNOS_CORE_KERNEL_HW_PCI

#include <kerntypes.h>

#include "Hardware/ACPI/acpi.h"

typedef struct PCIDeviceHeader {
  uint16_t _Vendor;
  uint16_t _Device;
  uint16_t _Command;
  uint16_t _Status;
  uint8_t  _Revision;
  uint8_t  _ProgramInterface;
  uint8_t  _Subclass;
  uint8_t  _Class;
  uint8_t  _CacheLineSize;
  uint8_t  _LatencyTimer;
  uint8_t  _HeaderType;
  uint8_t  _BIST;
} pcidevhdr_t;

typedef struct PCIDeviceHeader0 {
  pcidevhdr_t _Header;
  uint32_t    _BAR0;
  uint32_t    _BAR1;
  uint32_t    _BAR2;
  uint32_t    _BAR3;
  uint32_t    _BAR4;
  uint32_t    _BAR5;
  uint32_t    _CardBusCISPointer;
  uint16_t    _SubsystemVendor;
  uint16_t    _Subsystem;
  uint32_t    _ExpansionROMBase;
  uint8_t     _CapabilitiesPointer;
  uint8_t     _ReservedZero;
  uint16_t    _ReservedOne;
  uint32_t    _ReservedTwo;
  uint8_t     _InterruptLine;
  uint8_t     _InterruptPin;
  uint8_t     _MinimumGrant;
  uint8_t     _MaximumLatency;
} pcidevhdr0_t;

void kernel_hw_pci_enumerate(mcfghdr_t *__mcfgtable);

#endif
