/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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

#include <arch/context.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct com_isr com_isr_t;
typedef void (*com_intf_isr_t)(com_isr_t *isr, arch_context_t *ctx);
typedef void (*com_intf_eoi_t)(com_isr_t *isr);

typedef struct com_isr {
  com_intf_isr_t func;
  com_intf_eoi_t eoi;
  uintmax_t      id;
} com_isr_t;

bool com_sys_interrupt_set(bool status);
void com_sys_interrupt_register(uintmax_t      vec,
                                com_intf_isr_t func,
                                com_intf_eoi_t eoi);
void com_sys_interrupt_isr(uintmax_t vec, arch_context_t *ctx);
