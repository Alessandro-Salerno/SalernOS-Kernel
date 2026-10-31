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

#include <arch/cpu.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <lib/spinlock.h>
#include <lib/util.h>
#include <stdatomic.h>
#include <stdint.h>
#include <vendor/tailq.h>

// SYSCALL: exit_thread()
COM_SYS_SYSCALL(com_sys_syscall_exit_thread) {
    COM_SYS_SYSCALL_UNUSED_START(0);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();

    KASSERT(E_COM_THREAD_STATE_RUNNING ==
            __atomic_load_n(&curr_thread->state, __ATOMIC_RELAXED));
    kspinlock_acquire(&curr_thread->proc->threads_lock);
    __atomic_store_n(&curr_thread->exit_requested, true, __ATOMIC_RELEASE);
    bool last = com_sys_proc_all_threads_exiting_nolock(curr_thread->proc);
    kspinlock_release(&curr_thread->proc->threads_lock);

    if (last) {
        // Other exiting threads may still be switching out. Join their
        // acknowledgements before releasing shared process resources
        com_sys_proc_terminate(0);
    }

    com_sys_thread_exit(curr_thread);
    __builtin_unreachable();
}
