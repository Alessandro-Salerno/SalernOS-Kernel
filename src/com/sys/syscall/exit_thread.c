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
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <lib/util.h>
#include <stdatomic.h>
#include <stdint.h>
#include <vendor/tailq.h>

// SYSCALL: exit_thread()
COM_SYS_SYSCALL(com_sys_syscall_exit_thread) {
    COM_SYS_SYSCALL_UNUSED_START(0);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();

    com_spinlock_acquire(&curr_thread->sched_lock);

    KASSERT(curr_thread->runnable);
    KASSERT(!curr_thread->proc->exited);

    curr_thread->runnable = false;
    curr_thread->exited   = true;
    KASSERT(0 == curr_thread->pending_signals);

    com_spinlock_acquire(&curr_thread->proc->threads_lock);
    com_sys_proc_remove_thread_nolock(curr_thread->proc, curr_thread);
    if (TAILQ_EMPTY(&curr_thread->proc->threads)) {
        com_sys_proc_exit(curr_thread->proc, 0);
    }

    com_spinlock_release(&curr_thread->proc->threads_lock);
    com_spinlock_release(&curr_thread->sched_lock);
    com_sys_sched_yield();

    __builtin_unreachable();
}
