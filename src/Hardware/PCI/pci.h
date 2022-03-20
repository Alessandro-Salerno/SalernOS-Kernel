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


#ifndef SALERNOS_CORE_KERNEL_HW_PCI
#define SALERNOS_CORE_KERNEL_HW_PCI

    #include "Hardware/ACPI/acpi.h"
    #include <kerntypes.h>


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


    /**********************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    **********************************************************************/
    void            kernel_hw_pci_enumerate       (mcfghdr_t* __mcfgtable);

#endif
