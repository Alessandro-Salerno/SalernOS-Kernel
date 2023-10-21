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

#include "Hardware/Drivers/AHCI/ahci.h"
#include <kmem.h>
#include <kstdio.h>

#include "Memory/pgfalloc.h"
#include "kernelpanic.h"

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
#define HBA_PXIS_TFES (1 << 30)

#define FIS_TYPE_REG_H2D   0x27
#define FIS_TYPE_REG_D2D   0x34
#define FIS_TYPE_DMA_ACT   0x39
#define FIS_TYPE_DMA_SETUP 0x41
#define FIS_TYPE_DATA      0x46
#define FIS_TYPE_BIST      0x58
#define FIS_TYPE_PIO_SETUP 0x5F
#define FIS_TYPE_DEV_BITS  0xA1

#define FIS_MODE_LBA 1 << 6

#define ATA_CMD_READ_DMA_EX 0x25

#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ  0x08

static hbaporttype_t __check_port__(hbaport_t *__port) {
  uint32_t _status = __port->_SATAStatus;

  uint8_t _intf_power_management = (_status >> 8) & 0b111;
  uint8_t _dev_detection         = _status & 0b111;

  SOFTASSERT(_dev_detection == PORT_PRESENT, AHCI_PORT_NONE);
  SOFTASSERT(_intf_power_management == PORT_ACTIVE, AHCI_PORT_NONE);

  switch (__port->_Signature) {
  case SIG_ATAPI:
    return AHCI_PORT_SATAPI;
  case SIG_ATA:
    return AHCI_PORT_SATA;
  case SIG_PM:
    return AHCI_PORT_PM;
  case SIG_SEMB:
    return AHCI_PORT_SEMB;
  }

  return AHCI_PORT_NONE;
}

static void __stop_port__(ahciport_t *__port) {
  __port->_HBAPort->_CommandStatus &= ~HBA_PXCMD_ST;
  __port->_HBAPort->_CommandStatus &= ~HBA_PXCMD_FRE;

  while (__port->_HBAPort->_CommandIssue & HBA_PXCMD_FR &&
         __port->_HBAPort->_CommandIssue & HBA_PXCMD_CR)
    ;
}

static void __start_port__(ahciport_t *__port) {
  while (__port->_HBAPort->_CommandStatus & HBA_PXCMD_CR)
    ;
  __port->_HBAPort->_CommandStatus |= HBA_PXCMD_FRE | HBA_PXCMD_ST;
}

static bool __wait__(ahciport_t *__port) {
  uint64_t _spin = 0;
  while ((__port->_HBAPort->_TaskFileData & (ATA_DEV_BUSY | ATA_DEV_DRQ)) &&
         _spin++ < 1000000)
    ;
  return _spin < 1000000;
}

static void __configure_port__(ahciport_t *__port) {
  __stop_port__(__port);

  void *_new_base                    = kernel_pgfa_page_new();
  __port->_HBAPort->_CommandListBase = (uint64_t)(_new_base);
  kmemset(_new_base, 1024, 0);

  void *_fis_base                   = kernel_pgfa_page_new();
  __port->_HBAPort->_FisBaseAddress = (uint64_t)(_fis_base);
  kmemset(_fis_base, 256, 0);

  hbacmdhdr_t *_cmdhdr = (hbacmdhdr_t *)(_new_base);

  for (uint32_t _i = 0; _i < MAX_CMDHDR; _i++) {
    _cmdhdr[_i]._PRDTLength = 8;

    void    *_cmdtb = kernel_pgfa_page_new();
    uint64_t _addr  = (uint64_t)(_cmdtb) + (_i << 8);

    _cmdhdr[_i]._CommandTableBaseAddress = (uint64_t)(_addr);

    kmemset((void *)(_addr), 256, 0);
  }

  __start_port__(__port);
}

void kernel_hw_ahci_ports_probe(ahcidevdr_t *__dev) {
  uint32_t _ports_impl = __dev->_ABAR->_PortsImpplemented;

  for (uint32_t _i = 0; _i < MAX_PORTS; _i++) {
    if (_ports_impl & (1 << _i)) {
      hbaporttype_t _ptype = __check_port__(&__dev->_ABAR->_HBAPorts[_i]);

      if (_ptype == AHCI_PORT_SATA || _ptype == AHCI_PORT_SATAPI) {
        __dev->_Ports[__dev->_NPorts] =
            (ahciport_t){._HBAPort     = &__dev->_ABAR->_HBAPorts[_i],
                         ._HBAPortType = _ptype,
                         ._PortNum     = __dev->_NPorts};

        __dev->_NPorts++;
      }
    }
  }

  for (uint32_t _i = 0; _i < __dev->_NPorts; _i++) {
    ahciport_t _port = __dev->_Ports[_i];
    __configure_port__(&_port);

    _port._DMABuffer = (uint8_t *)(kernel_pgfa_page_new());
    kmemset(_port._DMABuffer, 4096, 0);
  }
}

bool kernel_hw_ahci_read(ahciport_t *__port,
                         uint64_t    __sector,
                         uint64_t    __sectors,
                         void       *__buff) {
  uint32_t _sec_low  = (uint32_t)(__sector),
           _sec_high = (uint32_t)(__sector >> 32);

  // Clear interrupt status
  __port->_HBAPort->_InterruptStatus = (uint32_t)(-1);

  hbacmdhdr_t *_cmdhdr = (hbacmdhdr_t *)(__port->_HBAPort->_CommandListBase);

  _cmdhdr->_CommandFISLength = sizeof(ahcifish2d_t) / sizeof(uint32_t);
  _cmdhdr->_Write            = FALSE;
  _cmdhdr->_PRDTLength       = 1;

  hbacmdtb_t *_cmdtb = (hbacmdtb_t *)(_cmdhdr->_CommandTableBaseAddress);
  kmemset(_cmdtb,
          sizeof(hbacmdtb_t) + _cmdhdr->_PRDTLength * sizeof(hbaprdtent_t),
          0);

  _cmdtb->_Entries[0]._DataBaseAddress       = (uint64_t)(__buff);
  _cmdtb->_Entries[0]._ByteCount             = __sectors * 512 - 1;
  _cmdtb->_Entries[0]._InterruptOnCompletion = TRUE;

  ahcifish2d_t *_fis    = (ahcifish2d_t *)(&_cmdtb->_CommandFIS);
  _fis->_FISType        = FIS_TYPE_REG_D2D;
  _fis->_CommandControl = TRUE;
  _fis->_Command        = ATA_CMD_READ_DMA_EX;
  _fis->_LBA0           = (uint8_t)(_sec_low);
  _fis->_LBA1           = (uint8_t)(_sec_low >> 8);
  _fis->_LBA2           = (uint8_t)(_sec_low >> 16);
  _fis->_LBA3           = (uint8_t)(_sec_high);
  _fis->_LBA4           = (uint8_t)(_sec_high >> 8);
  _fis->_LBA5           = (uint8_t)(_sec_high >> 16);
  _fis->_DeviceRegister = FIS_MODE_LBA;
  _fis->_CountLow       = __sectors & 0xff;
  _fis->_CountHigh      = (__sectors << 8) & 0xff;

  if (!__wait__(__port))
    return FALSE;

  __port->_HBAPort->_CommandIssue = TRUE;

  // Temporary code
  while (TRUE) {
    if (__port->_HBAPort->_CommandIssue == 0)
      break;
    if (__port->_HBAPort->_InterruptStatus & HBA_PXIS_TFES)
      return FALSE;
  }

  return TRUE;
}
