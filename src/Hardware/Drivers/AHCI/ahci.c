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
#include "kernelpanic.h"
#include <kstdio.h>

#define MAX_PORTS 32

#define PORT_PRESENT 0x03
#define PORT_ACTIVE  0x01

#define SIG_ATAPI 0xEB140101
#define SIG_ATA   0x00000101
#define SIG_SEMB  0xC33C0101
#define SIG_PM    0x96690101


static ahciport_t __check_port__(hbaport_t* __port) {
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


void kernel_hw_ahci_ports_probe(ahcidevdr_t* __dev) {
    uint32_t _ports_impl = __dev->_ABAR->_PortsImpplemented;

    for (uint32_t _i = 0; _i < MAX_PORTS; _i++) {
        if (_ports_impl & (1 << _i)) {
            ahciport_t _ptype = __check_port__(&__dev->_ABAR->_HBAPorts[_i]);

            if (_ptype == SIG_ATA || _ptype == SIG_ATAPI) {
                // We can do something with this
            }
        }
    }
}
