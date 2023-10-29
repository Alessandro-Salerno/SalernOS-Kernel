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

#include "sys/interrupts/idt.h"

#include "kernelpanic.h"
#include "mm/pgfalloc.h"
#include "sys/interrupts/handlers.h"
#include "sys/syscall/dispatcher.h"
#include "time/pit/pit.h"

#define IDT_DINIT \
  "Interrupt Descriptor Table Fault:\nKernel tried to reinitialize IDT."

static idtr_t idtr;
static bool   idtInitialized;

static idtdescent_t *__create_entry__(uint16_t __offset, void *__isr) {
  idtdescent_t *_handler =
      (idtdescent_t *)(idtr._Offset + __offset * sizeof(idtdescent_t));
  _handler->_TypesAndAttributes = IDT_TA_INTERRUPT_GATE;
  _handler->_Selector           = 0x08;

  idt_offset_set(_handler, (uint64_t)(__isr));
  return _handler;
}

void idt_initialize() {
  panic_assert(!idtInitialized, IDT_DINIT);

  idtr._Limit  = 0x1000 - 1;
  idtr._Offset = (uint64_t)(pgfa_page_new());

  __create_entry__(0x0E,
                   interrupt_handlers_pgfault); // Page fault handler
  __create_entry__(0x08,
                   interrupt_handlers_dfault); // Double fault handler
  __create_entry__(
      0x0D,
      interrupt_handlers_gpfault); // General protection fault handler
  __create_entry__(0x21,
                   interrupt_handlers_kbhit);      // Keyboard Interrupt hanlder
  __create_entry__(0x20, interrupt_handlers_tick); // PIT Tick interrupt handler
  __create_entry__(0x81,
                   syscall_dispatch); // System Call interrupt handler

  asm("lidt %0" : : "m"(idtr));

  idtInitialized = TRUE;
}

void idt_offset_set(idtdescent_t *__ent, uint64_t __offset) {
  __ent->_OffsetZero = (uint16_t)(__offset & 0x000000000000ffff);
  __ent->_OffsetOne  = (uint16_t)((__offset & 0x00000000ffff0000) >> 16);
  __ent->_OffsetTwo  = (uint32_t)((__offset & 0xffffffff00000000) >> 32);
}

uint64_t idt_offset_get(idtdescent_t *__ent) {
  return (uint64_t){(uint64_t)(__ent->_OffsetZero) |
                    (uint64_t)(__ent->_OffsetOne) << 16 |
                    (uint64_t)(__ent->_OffsetTwo) << 32};
}
