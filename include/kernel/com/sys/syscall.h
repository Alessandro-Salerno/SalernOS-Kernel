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
com_syscall_ret_t com_sys_syscall_read(arch_context_t *ctx,
                                       uintmax_t       fd,
                                       uintmax_t       bufptr,
                                       uintmax_t       buflen,
                                       uintmax_t       unused);
com_syscall_ret_t com_sys_syscall_execve(arch_context_t *ctx,
                                         uintmax_t       pathptr,
                                         uintmax_t       argvptr,
                                         uintmax_t       envptr,
                                         uintmax_t       unused);
com_syscall_ret_t com_sys_syscall_fork(arch_context_t *ctx,
                                       uintmax_t       unused1,
                                       uintmax_t       unused2,
                                       uintmax_t       unused3,
                                       uintmax_t       unused4);
com_syscall_ret_t com_sys_syscall_sysinfo(arch_context_t *ctx,
                                          uintmax_t       bufptr,
                                          uintmax_t       unused1,
                                          uintmax_t       unused2,
                                          uintmax_t       unused3);
com_syscall_ret_t com_sys_syscall_waitpid(arch_context_t *ctx,
                                          uintmax_t       pid,
                                          uintmax_t       statusptr,
                                          uintmax_t       flags,
                                          uintmax_t       unused);
com_syscall_ret_t com_sys_syscall_exit(arch_context_t *ctx,
                                       uintmax_t       status,
                                       uintmax_t       unused1,
                                       uintmax_t       unused2,
                                       uintmax_t       unused3);
com_syscall_ret_t com_sys_syscall_ioctl(arch_context_t *ctx,
                                        uintmax_t       fd,
                                        uintmax_t       op,
                                        uintmax_t       bufptr,
                                        uintmax_t       unused);
com_syscall_ret_t com_sys_syscall_open(arch_context_t *ctx,
                                       uintmax_t       pathptr,
                                       uintmax_t       pathlen,
                                       uintmax_t       flags,
                                       uintmax_t       unused);
