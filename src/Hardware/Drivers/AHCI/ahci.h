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


#ifndef SALERNOS_CORE_KERNEL_HW_DRIVERS_AHCI
#define SALERNOS_CORE_KERNEL_HW_DRIVERS_AHCI

    #include <kerntypes.h>
    #include "Hardware/PCI/pci.h"


    typedef enum AHCIPortTypes {
        AHCI_PORT_NONE,
        AHCI_PORT_SATA,
        AHCI_PORT_SEMB,
        AHCI_PORT_PM,
        AHCI_PORT_SATAPI
    } hbaporttype_t;

    typedef struct HBAPort {
        uint32_t _CommandListBase;
        uint32_t _CommandListBaseUpper;
        uint32_t _FisBaseAddress;
        uint32_t _FisBaseAddressUpper;
        uint32_t _InterruptStatus;
        uint32_t _InterruptEnable;
        uint32_t _CommandStatus;
        uint32_t _ReservedZero;
        uint32_t _TaskFileData;
        uint32_t _Signature;
        uint32_t _SATAStatus;
        uint32_t _SATAControl;
        uint32_t _SATAError;
        uint32_t _SATAActive;
        uint32_t _CommandIssue;
        uint32_t _SATANotification;
        uint32_t _FisSwitchControl;
        
        uint32_t _Reserved[11];
        uint32_t _VendorSpecific[4];
    } hbaport_t;

    typedef struct HBAMemory {
        uint32_t  _HostCapability;
        uint32_t  _GLobalHostControl;
        uint32_t  _InterruptStatus;
        uint32_t  _PortsImpplemented;
        uint32_t  _Version;
        uint32_t  _CCCControl;
        uint32_t  _CCCPorts;
        uint32_t  _EnclosureManagementLocation;
        uint32_t  _EnclosureManagementControl;
        uint32_t  _HostCapabilitiesExt;
        uint32_t  _BIOSHandoff;

        uint8_t   _Reserved[116];
        uint8_t   _VendorSpecific[96];
        hbaport_t _HBAPorts[1];
    } hbamem_t;

    typedef struct AHCIPort {
        hbaport_t*    _HBAPort;
        hbaporttype_t _HBAPortType;
        uint8_t*      _DMABuffer;
        uint8_t       _PortNum;
    } ahciport_t;

    typedef struct AHCIDeviceDriver {
        pcidevhdr_t* _PCIDeviceBase;
        hbamem_t*    _ABAR;

        ahciport_t   _Ports[32];
        uint8_t      _NPorts;
    } ahcidevdr_t;


    /*****************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    ******************************************************************/
    void            kernel_hw_ahci_ports_probe    (ahcidevdr_t* __dev);

#endif
