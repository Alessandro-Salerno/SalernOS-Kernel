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

    com_syscall_ret_t ret     = COM_SYS_SYSCALL_BASE_OK();
    int               sig_ret = com_sys_signal_set_mask(
        &curr_proc->masked_signals, how, set, oset, &curr_proc->signal_lock);

    if (0 != sig_ret) {
        ret.value = -1;
        ret.err   = sig_ret;
    }

    return ret;
}
