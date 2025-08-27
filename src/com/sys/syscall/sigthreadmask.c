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
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/util.h>

// SYSCALL: sigthreadmask(int how, sigset_t *set, sigset_t *oldset)
COM_SYS_SYSCALL(com_sys_syscall_ssigthreadmask) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int           how  = COM_SYS_SYSCALL_ARG(int, 1);
    com_sigset_t *set  = COM_SYS_SYSCALL_ARG(com_sigset_t *, 2);
    com_sigset_t *oset = COM_SYS_SYSCALL_ARG(com_sigset_t *, 3);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_syscall_ret_t ret = COM_SYS_SYSCALL_BASE_OK();
    kspinlock_acquire(&curr_proc->signal_lock);
    int sig_ret = com_ipc_signal_set_mask_nolock(
        &curr_thread->masked_signals, how, set, oset);

    if (0 != sig_ret) {
        ret.value = -1;
        ret.err   = sig_ret;
    }

    // NOTE: the current threads's pending signals need to be updated due to how
    // I handle the undefined behaaviour in sigprocmask Essentially, POSIX says
    // sigprocmask is unspecified for multi-threaded processes. This means that
    // I could just keep thread-local signals and remove stuff from proc struct.
    // HOWEVER, I'm taking this opportunity to add a feature: sigprocmask sets a
    // GLOBAL mask for all threads in the process. However, in signal_dispatch,
    // I check only the thread's pending signals (since the proces's are set
    // only if no thread can handle the signal due to its mask), so if something
    // was previously masked for this thread and is now unmasked and pending on
    // the process, it should become pending on the thread too
    com_sigmask_t new_pending = curr_proc->pending_signals &
                                ~(curr_thread->masked_signals) &
                                ~(curr_proc->masked_signals);
    curr_thread->pending_signals |= new_pending;
    curr_proc->pending_signals &=
        ~new_pending; // ensures that this is only done once

    kspinlock_release(&curr_proc->signal_lock);
    return ret;
}
