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

#include <errno.h>
#include <kernel/com/sys/signal.h>
#include <lib/mem.h>
#include <stdbool.h>
#include <vendor/tailq.h>

static int send_to_thread(com_thread_t *thread, int sig) {
    if (COM_SYS_SIGNAL_SIGMASK_ISSET(&thread->masked_signals, sig)) {
        return -1;
    }

    // TODO: finish this in next commit (notify waiting threads + stop/cont)
    // Also, remember not to send self-IPI because that could probably create
    // issues with like SIGSTOP

    return 0;
}

void com_sys_signal_sigset_emptY(com_sigset_t *set) {
    kmemset(set, sizeof(com_sigset_t), 0);
}

int com_sys_signal_send_to_proc(pid_t pid, int sig, com_proc_t *sender) {
    com_proc_t *proc = com_sys_proc_get_by_pid(pid);

    if (NULL == proc) {
        return ESRCH;
    }

    com_spinlock_acquire(&proc->threads_lock);
    if (proc->exited) {
        com_sys_proc_release_glock();
        return EPERM;
    }

    com_spinlock_acquire(&proc->signal_lock);
    com_thread_t *t, *_;
    bool          found = false;
    TAILQ_FOREACH_SAFE(t, &proc->threads, proc_threads, _) {
        if (0 == send_to_thread(t, sig)) {
            found = true;
            break;
        }
    }

    if (!found) {
        COM_SYS_SIGNAL_SIGMASK_SET(&proc->pending_signals, sig);
    }

    com_spinlock_release(&proc->signal_lock);
    com_spinlock_release(&proc->threads_lock);

    return 0;
}

int com_sys_signal_send_to_proc_group(pid_t pgid, int sig, com_proc_t *sender) {
    com_proc_group_t *group = com_sys_proc_get_group_by_pgid(pgid);

    if (NULL == group) {
        return ESRCH;
    }

    com_spinlock_acquire(&group->procs_lock);
    com_proc_t *p, *_;
    TAILQ_FOREACH_SAFE(p, &group->procs, procs, _) {
        com_sys_signal_send_to_proc(p->pid, sig, sender);
    }
    com_spinlock_release(&group->procs_lock);

    return 0;
}

int com_sys_signal_send_to_thread(struct com_thread *thread,
                                  int                sig,
                                  com_proc_t        *sender) {
    // TODO: actually check that this can be done (sender check)
    com_spinlock_acquire(&thread->proc->signal_lock);
    int ret = send_to_thread(thread, sig);
    com_spinlock_release(&thread->proc->signal_lock);
    return ret;
}

void com_sys_signal_dispatch(arch_context_t *ctx) {
}

int com_sys_signal_set_mask(com_sigmask_t  *mask,
                            int             how,
                            com_sigset_t   *set,
                            com_sigset_t   *oset,
                            com_spinlock_t *signal_lock) {
    com_spinlock_acquire(signal_lock);
    com_sigmask_t old_mask = *mask;

    if (NULL == set) {
        goto noset;
    }

    switch (how) {
    case SIG_BLOCK:
        *mask |= set->sig[0];
        break;
    case SIG_SETMASK:
        *mask = set->sig[0];
        break;
    case SIG_UNBLOCK:
        *mask &= set->sig[0];
        break;
    default:
        com_spinlock_release(signal_lock);
        return EINVAL;
    }

noset:
    if (NULL != oset) {
        com_sys_signal_sigset_emptY(oset);
        oset->sig[0] = old_mask;
    }

    com_spinlock_release(signal_lock);
    return 0;
}
