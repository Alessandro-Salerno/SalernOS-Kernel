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


#ifndef SALERNOS_CORE_KERNEL_HW_ACPI
#define SALERNOS_CORE_KERNEL_HW_ACPI

    #include <kerntypes.h>
    #include <sbs.h>


    typedef struct __attribute__((packed)) SystemDescriptorTableHeader {
        uint8_t  _Signature[4];
        uint32_t _Length;
        uint8_t  _Revision;
        uint8_t  _Checksum;
        uint8_t  _OEM[6];
        uint8_t  _OEMTableID[8];
        uint32_t _OEMRevision;
        uint32_t _Creator;
        uint32_t _CreatorRevision;
    } sdthdr_t;

    typedef struct __attribute__((packed)) MCFGHeader {
        sdthdr_t _SDTHeader;
        uint64_t _Reserved;
    } mcfghdr_t;

    typedef struct __attribute__((packed)) ACPIDeviceConfiguration {
        uint64_t _BaseAddress;
        uint16_t _PCISegGroup;
        uint8_t  _StartBus;
        uint8_t  _EndBus;
        uint32_t _Reserved;
    } acpidevconf_t;

    typedef struct ACPIInfo {
        sdthdr_t*  _XSDT;
        mcfghdr_t* _MCFG;
    } acpiinfo_t;

    typedef struct SimpleBootRootSystemDescriptor rsdp_t;


    /*************************************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    *************************************************************************************/
    void*           kernel_hw_acpi_table_find     (sdthdr_t* __sdthdr, char* __signature);

#endif
