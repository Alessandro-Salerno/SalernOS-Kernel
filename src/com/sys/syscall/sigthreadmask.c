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

// SYSCALL: sigthreadmask(int how, sigset_t *set, sigset_t *oldset)
COM_SYS_SYSCALL(com_sys_syscall_ssigthreadmask) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int           how  = COM_SYS_SYSCALL_ARG(int, 1);
    com_sigset_t *set  = COM_SYS_SYSCALL_ARG(com_sigset_t *, 2);
    com_sigset_t *oset = COM_SYS_SYSCALL_ARG(com_sigset_t *, 3);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();

    // Unlike with sigprocmask, a thread cannot have race conditiosn on
    // curr_thread->masked_signals, so there's no need to use atomic operations
    // If you pass in pointers to memory that is shared among mutiple theards,
    // that's your fault!

    if (NULL != oset) {
        com_sys_signal_sigset_emptY(oset);
        oset->sig[0] = curr_thread->masked_signals;
    }

    if (NULL == set) {
        goto noset;
    }

    switch (how) {
    case SIG_BLOCK:
        curr_thread->masked_signals |= set->sig[0];
        break;
    case SIG_SETMASK:
        curr_thread->masked_signals = set->sig[0];
        break;
    case SIG_UNBLOCK:
        curr_thread->masked_signals &= set->sig[0];
        break;
    default:
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

noset:
    return COM_SYS_SYSCALL_OK(0);
}
