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
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/context.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

com_syscall_ret_t com_sys_syscall_clone(arch_context_t *ctx,
                                        uintmax_t       new_ip,
                                        uintmax_t       new_sp,
                                        uintmax_t       unused,
                                        uintmax_t       unused1) {
    (void)ctx;
    (void)unused;
    (void)unused1;

    com_thread_t *curr_thread = hdr_arch_cpu_get_thread();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_thread_t *new_thread = com_sys_thread_new(curr_proc, NULL, 0, NULL);
    ARCH_CONTEXT_CLONE(new_thread, new_ip, new_sp);
    ARCH_CONTEXT_INIT_EXTRA(new_thread->xctx);

    return (com_syscall_ret_t){new_thread->tid, 0};
}
