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
#include <kernel/com/sys/signal.h>
#include <kernel/com/sys/syscall.h>

// SYSCALL: sigpending(sigset_t *set)
COM_SYS_SYSCALL(com_sys_syscall_sigpending) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    com_sigset_t *sigset = COM_SYS_SYSCALL_ARG(com_sigset_t *, 1);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_sys_signal_sigset_emptY(sigset);
    com_spinlock_acquire(&curr_proc->signal_lock);
    sigset->sig[0] = curr_thread->pending_signals;
    com_spinlock_release(&curr_proc->signal_lock);

    return COM_SYS_SYSCALL_OK(0);
}
