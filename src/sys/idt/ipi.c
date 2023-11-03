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

#include <stddef.h>

#include "sys/idt/idt.h"
#include "sys/idt/ipi.h"

uint8_t sys_idt_ipi_vecPanic;

void sys_idt_ipi_initialize() {
  // Temporary code
  sys_idt_ipi_vecPanic = sys_idt_vector_allocate();
  sys_idt_isr_register(sys_idt_ipi_vecPanic, sys_idt_ipi_entry_panic, 0x8e);
  sys_idt_isr_set(sys_idt_ipi_vecPanic, sys_idt_ipi_entry_panic);
}
