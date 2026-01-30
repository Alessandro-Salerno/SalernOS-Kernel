/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#pragma once

#include <kernel/com/sys/interrupt.h>

#define X86_64_LAPIC_TIMER_INTERRUPT 0x30

void x86_64_lapic_eoi(com_isr_t *isr);
void x86_64_lapic_bsp_init(void);
void x86_64_lapic_init(void);
void x86_64_lapic_selfipi(uint8_t vector);
void x86_64_lapic_send_ipi(uint8_t apic_id, uint8_t vector);
void x86_64_lapic_broadcast_ipi(uint8_t vector);
