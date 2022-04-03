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


#include "Hardware/Drivers/AHCI/ahci.h"
#include "Memory/pgfalloc.h"
#include "kernelpanic.h"
#include <kstdio.h>
#include <kmem.h>

#define MAX_PORTS  32
#define MAX_CMDHDR 32

#define PORT_PRESENT 0x03
#define PORT_ACTIVE  0x01

#define SIG_ATAPI 0xEB140101
#define SIG_ATA   0x00000101
#define SIG_SEMB  0xC33C0101
#define SIG_PM    0x96690101

#define HBA_PXCMD_CR  0x8000
#define HBA_PXCMD_FRE 0x0010
#define HBA_PXCMD_ST  0x0001
#define HBA_PXCMD_FR  0x4000


static hbaporttype_t __check_port__(hbaport_t* __port) {
    uint32_t _status = __port->_SATAStatus;
    
    uint8_t _intf_power_management = (_status >> 8) & 0b111;
    uint8_t _dev_detection         = _status & 0b111;

    SOFTASSERT(_dev_detection == PORT_PRESENT, AHCI_PORT_NONE);
    SOFTASSERT(_intf_power_management == PORT_ACTIVE, AHCI_PORT_NONE);
    
    switch (__port->_Signature) {
        case SIG_ATAPI  : return AHCI_PORT_SATAPI;
        case SIG_ATA    : return AHCI_PORT_SATA;
        case SIG_PM     : return AHCI_PORT_PM;
        case SIG_SEMB   : return AHCI_PORT_SEMB;
    }

    return AHCI_PORT_NONE;
}

static void __stop_port__(ahciport_t* __port) {
    __port->_HBAPort->_CommandStatus &= ~HBA_PXCMD_ST;
    __port->_HBAPort->_CommandStatus &= ~HBA_PXCMD_FRE;

    while (__port->_HBAPort->_CommandIssue & HBA_PXCMD_FR && __port->_HBAPort->_CommandIssue & HBA_PXCMD_CR);
}

static void __start_port__(ahciport_t* __port) {
    while (__port->_HBAPort->_CommandStatus & HBA_PXCMD_CR);
    __port->_HBAPort->_CommandStatus |= HBA_PXCMD_FRE | HBA_PXCMD_ST;
}

static void __configure_port__(ahciport_t* __port) {
    __stop_port__(__port);

    void* _new_base = kernel_pgfa_page_new();
    __port->_HBAPort->_CommandListBase      = (uint32_t)(uint64_t)(_new_base);
    __port->_HBAPort->_CommandListBaseUpper = (uint32_t)((uint64_t)(_new_base) >> 32);
    kmemset(_new_base, 1024, 0);

    void* _fis_base = kernel_pgfa_page_new();
    __port->_HBAPort->_FisBaseAddress      = (uint32_t)(uint64_t)(_fis_base);
    __port->_HBAPort->_FisBaseAddressUpper = (uint32_t)((uint64_t)(_fis_base) >> 32);
    kmemset(_fis_base, 256, 0);

    hbacmdhdr_t* _cmdhdr = (hbacmdhdr_t*)(_new_base);

    for (uint32_t _i = 0; _i < MAX_CMDHDR; _i++) {
        _cmdhdr[_i]._PRDTLength = 8;

        void*    _cmdtb = kernel_pgfa_page_new();
        uint64_t _addr  = (uint64_t)(_cmdtb) + (_i << 8);

        _cmdhdr[_i]._CommandTableBaseAddress      = (uint32_t)(_addr);
        _cmdhdr[_i]._CommandTableBaseAddressUpper = (uint32_t)(_addr >> 32);

        kmemset((void*)(_addr), 256, 0);
    }

    __start_port__(__port);
}


void kernel_hw_ahci_ports_probe(ahcidevdr_t* __dev) {
    uint32_t _ports_impl = __dev->_ABAR->_PortsImpplemented;

    for (uint32_t _i = 0; _i < MAX_PORTS; _i++) {
        if (_ports_impl & (1 << _i)) {
            hbaporttype_t _ptype = __check_port__(&__dev->_ABAR->_HBAPorts[_i]);

            if (_ptype == AHCI_PORT_SATA || _ptype == AHCI_PORT_SATAPI) {
                __dev->_Ports[__dev->_NPorts] = (ahciport_t) {
                    ._HBAPort     = &__dev->_ABAR->_HBAPorts[_i],
                    ._HBAPortType = _ptype,
                    ._PortNum     = __dev->_NPorts
                };

                __configure_port__(&__dev->_Ports[__dev->_NPorts]);
                __dev->_NPorts++;
            }
        }
    }
}
