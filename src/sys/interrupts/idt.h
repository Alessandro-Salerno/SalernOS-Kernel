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

#pragma once

#include <kerntypes.h>

#define IDT_TA_INTERRUPT_GATE 0b10001110
#define IDT_TA_CALL_GATE      0b10001100
#define IDT_TA_TRAP_GATE      0b10001111

typedef struct __attribute__((packed)) IDTDescriptorEntry {
  uint16_t _OffsetZero;
  uint16_t _Selector;
  uint8_t  _InterruptStackTableOffset;
  uint8_t  _TypesAndAttributes;
  uint16_t _OffsetOne;
  uint32_t _OffsetTwo;
  uint32_t _Ignore;
} idtdescent_t;

typedef struct __attribute__((packed)) InterruptDescriptorTableRegister {
  uint16_t _Limit;
  uint64_t _Offset;
} idtr_t;

void     kernel_idt_initialize();
void     kernel_idt_offset_set(idtdescent_t *__ent, uint64_t __offset);
uint64_t kernel_idt_offset_get(idtdescent_t *__ent);
