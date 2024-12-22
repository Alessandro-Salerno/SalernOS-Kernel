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
#include <stdint.h>

typedef struct {
  uintmax_t value;
  uintmax_t err;
} com_syscall_ret_t;

typedef com_syscall_ret_t (*com_intf_syscall_t)(arch_context_t *ctx,
                                                uintmax_t       arg1,
                                                uintmax_t       arg2,
                                                uintmax_t       arg3,
                                                uintmax_t       arg4);

void com_sys_syscall_register(uintmax_t number, com_intf_syscall_t handler);
void com_sys_syscall_init(void);

// SYSCALLS

com_syscall_ret_t com_sys_syscall_write(arch_context_t *ctx,
                                        uintmax_t       fd,
                                        uintmax_t       bufptr,
                                        uintmax_t       buflen,
                                        uintmax_t       unused);
