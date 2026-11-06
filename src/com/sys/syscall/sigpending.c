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
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/mem.h>
#include <lib/usercopy.h>

// SYSCALL: sigpending(sigset_t *set)
COM_SYS_SYSCALL(com_sys_syscall_sigpending) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    com_sigset_t *sigset = COM_SYS_SYSCALL_ARG(com_sigset_t *, 1);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_sigset_t kset;
    com_ipc_signal_sigset_emptY(&kset);
    kspinlock_acquire(&curr_proc->signal_lock);
    kset.sig[0] = curr_thread->pending_signals;
    kspinlock_release(&curr_proc->signal_lock);
    int copy_ret = kmemcpy_to_user(sigset, &kset, sizeof(kset));
    if (0 != copy_ret) {
        return COM_SYS_SYSCALL_ERR(copy_ret);
    }

    return COM_SYS_SYSCALL_OK(0);
}
