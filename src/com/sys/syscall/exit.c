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
#include <arch/mmu.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/mmu.h>
#include <lib/util.h>
#include <stdint.h>

// TODO: what happens if two threads of the same process call exit?
// SYSCALL: exit(int status)
COM_SYS_SYSCALL(com_sys_syscall_exit) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    int exit_status = COM_SYS_SYSCALL_ARG(int, 1);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;
    // KDEBUG(
    // "exiting pid=%u with num_ref=%u", curr_proc->pid, curr_proc->num_ref);

    com_sys_proc_exit(curr_proc, exit_status);
    com_spinlock_acquire(&curr_thread->sched_lock);
    curr_thread->runnable = false;
    curr_thread->exited   = true;
    com_spinlock_release(&curr_thread->sched_lock);
    com_sys_sched_yield();

    __builtin_unreachable();
}
