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
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/context.h>
#include <lib/spinlock.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

// SYSCALL: clone(void *new_ip, void *new_sp)
COM_SYS_SYSCALL(com_sys_syscall_clone) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    void *new_ip = COM_SYS_SYSCALL_ARG(void *, 1);
    void *new_sp = COM_SYS_SYSCALL_ARG(void *, 2);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_thread_t *new_thread = com_sys_thread_new(curr_proc, NULL, 0, NULL);
    ARCH_CONTEXT_CLONE(new_thread, new_ip, new_sp);
    ARCH_CONTEXT_INIT_EXTRA(new_thread->xctx);

    com_sys_thread_ready(new_thread);

    return COM_SYS_SYSCALL_OK(new_thread->tid);
}
