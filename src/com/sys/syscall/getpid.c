/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
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

#include <arch/cpu.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>

com_syscall_ret_t com_sys_syscall_getpid(arch_context_t *ctx,
                                         uintmax_t       unused,
                                         uintmax_t       unused1,
                                         uintmax_t       unused2,
                                         uintmax_t       unused3) {
    (void)ctx;
    (void)unused;
    (void)unused1;
    (void)unused2;
    (void)unused3;

    return (com_syscall_ret_t){hdr_arch_cpu_get_thread()->proc->pid, 0};
}
