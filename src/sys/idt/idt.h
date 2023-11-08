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

#include <stdint.h>

extern void *isrThunkLookup[];

void    sys_idt_isr_register(uint8_t __vec, void *__isr, uint8_t __flags);
uint8_t sys_idt_vector_allocate();
void    sys_idt_isr_set(uint8_t __vec, void *__isr);
void   *sys_idt_isr_get(uint8_t __vec);
void    sys_idt_ist_set(uint8_t __vec, uint8_t __ist);
uint8_t sys_idt_ist_get(uint8_t __vec);
void    sys_idt_flags_set(uint8_t __vec, uint8_t __flags);
uint8_t sys_idt_flags_get(uint8_t __vec);
void    sys_idt_reload();
void    sys_idt_initialize();
