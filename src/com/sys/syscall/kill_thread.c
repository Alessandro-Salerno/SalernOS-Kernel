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

// SYSCALL: kill_thread(pid_t tid, int sig)
COM_SYS_SYSCALL(com_sys_syscall_kill_thread) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    pid_t tid = COM_SYS_SYSCALL_ARG(pid_t, 1);
    int   sig = COM_SYS_SYSCALL_ARG(int, 2);

    com_syscall_ret_t ret         = {0, 0};
    com_thread_t     *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t       *curr_proc   = curr_thread->proc;

    if (0 == sig) {
        goto skip_send;
    }

    com_spinlock_acquire(&curr_proc->threads_lock);
    com_thread_t *t, *_;
    bool          found = false;
    TAILQ_FOREACH_SAFE(t, &curr_proc->threads, proc_threads, _) {
        if (t->tid == tid) {
            found = true;
            break;
        }
    }
    if (found) {
        ret.err = com_sys_signal_send_to_thread(t, sig, curr_proc);
    } else {
        ret.err = ESRCH;
    }
    com_spinlock_release(&curr_proc->threads_lock);

skip_send:
    if (0 != ret.err) {
        ret.value = -1;
    }

    return ret;
}
