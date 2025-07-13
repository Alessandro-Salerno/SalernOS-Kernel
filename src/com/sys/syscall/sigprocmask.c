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
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/signal.h>
#include <kernel/com/sys/syscall.h>
#include <signal.h>

// SYSCALL: sigprocmask(int how, sigset_t *set, sigset_t *oset)
COM_SYS_SYSCALL(com_sys_syscall_sigprocmask) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int           how  = COM_SYS_SYSCALL_ARG(int, 1);
    com_sigset_t *set  = COM_SYS_SYSCALL_ARG(com_sigset_t *, 2);
    com_sigset_t *oset = COM_SYS_SYSCALL_ARG(com_sigset_t *, 3);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_sigmask_t old_mask =
        __atomic_load_n(&curr_proc->masked_signals, __ATOMIC_SEQ_CST);

    if (NULL == set) {
        goto noset;
    }

    switch (how) {
    case SIG_BLOCK:
        __atomic_fetch_or(
            &curr_proc->masked_signals, set->sig[0], __ATOMIC_SEQ_CST);
        break;
    case SIG_SETMASK:
        __atomic_store_n(
            &curr_proc->masked_signals, set->sig[0], __ATOMIC_SEQ_CST);
        break;
    case SIG_UNBLOCK:
        __atomic_fetch_and(
            &curr_proc->masked_signals, set->sig[0], __ATOMIC_SEQ_CST);
        break;
    default:
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

noset:
    if (NULL != oset) {
        com_sys_signal_sigset_emptY(oset);
        oset->sig[0] = old_mask;
    }

    return COM_SYS_SYSCALL_OK(0);
}
