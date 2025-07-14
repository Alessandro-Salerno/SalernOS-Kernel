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
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/signal.h>
#include <lib/mem.h>
#include <lib/util.h>
#include <stdbool.h>
#include <vendor/tailq.h>

#define IS_VALID_SIGNAL(sig) ((sig) > 0 && (sig) < 64)

// CREDIT: OpenBSD
#define SA_KILL     0x01 /* terminates process by default */
#define SA_CORE     0x02 /* ditto and coredumps */
#define SA_STOP     0x04 /* suspend process */
#define SA_TTYSTOP  0x08 /* ditto, from tty */
#define SA_IGNORE   0x10 /* ignore by default */
#define SA_CONT     0x20 /* continue if suspended */
#define SA_CANTMASK 0x40 /* non-maskable, catchable */

// CREDIT: vloxei64/ke
static const int SignalProperties[] = {
    [SIGHUP]    = SA_KILL,
    [SIGINT]    = SA_KILL,
    [SIGQUIT]   = SA_KILL | SA_CORE,
    [SIGILL]    = SA_KILL | SA_CORE,
    [SIGTRAP]   = SA_KILL | SA_CORE,
    [SIGABRT]   = SA_KILL | SA_CORE,
    [SIGFPE]    = SA_KILL | SA_CORE,
    [SIGKILL]   = SA_KILL,
    [SIGBUS]    = SA_KILL | SA_CORE,
    [SIGSEGV]   = SA_KILL | SA_CORE,
    [SIGSYS]    = SA_KILL | SA_CORE,
    [SIGPIPE]   = SA_KILL,
    [SIGALRM]   = SA_KILL,
    [SIGTERM]   = SA_KILL,
    [SIGURG]    = SA_IGNORE,
    [SIGSTOP]   = SA_STOP,
    [SIGTSTP]   = SA_STOP | SA_TTYSTOP,
    [SIGCONT]   = SA_IGNORE | SA_CONT,
    [SIGCHLD]   = SA_IGNORE,
    [SIGTTIN]   = SA_STOP | SA_TTYSTOP,
    [SIGTTOU]   = SA_STOP | SA_TTYSTOP,
    [SIGIO]     = SA_IGNORE,
    [SIGXCPU]   = SA_KILL,
    [SIGXFSZ]   = SA_KILL,
    [SIGVTALRM] = SA_KILL,
    [SIGPROF]   = SA_KILL,
    [SIGWINCH]  = SA_IGNORE,
    [SIGUSR1]   = SA_KILL,
    [SIGUSR2]   = SA_KILL,
};

static int send_to_thread(com_thread_t *thread, int sig) {
    if (COM_SYS_SIGNAL_SIGMASK_ISSET(&thread->masked_signals, sig)) {
        return -1;
    }

    COM_SYS_SIGNAL_SIGMASK_SET(&thread->pending_signals, sig);

    if (thread->proc->stopped && SIGCONT != sig) {
        return -2;
    }

    com_spinlock_acquire(&thread->sched_lock);
    if (NULL != thread->waiting_on) {
        com_spinlock_t *wait_cond = thread->waiting_cond;
        com_spinlock_acquire(wait_cond);
        com_sys_sched_notify_thread_nolock(thread);
        com_spinlock_release(wait_cond);
    } else if (!thread->runnable) {
        com_sys_thread_ready_nolock(thread);
    }
    com_spinlock_release(&thread->sched_lock);

    // TODO:, remember not to send self-IPI because that could probably create
    // issues with like SIGSTOP

    return 0;
}

void com_sys_signal_sigset_emptY(com_sigset_t *set) {
    kmemset(set, sizeof(com_sigset_t), 0);
}

int com_sys_signal_send_to_proc(pid_t pid, int sig, com_proc_t *sender) {
    if (!IS_VALID_SIGNAL(sig)) {
        return EINVAL;
    }

    com_proc_t *proc = com_sys_proc_get_by_pid(pid);

    if (NULL == proc) {
        return ESRCH;
    }

    com_spinlock_acquire(&proc->signal_lock);
    if (proc->exited) {
        return ESRCH;
    }

    com_spinlock_acquire(&proc->threads_lock);
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

    com_spinlock_release(&proc->threads_lock);
    com_spinlock_release(&proc->signal_lock);

    return 0;
}

int com_sys_signal_send_to_proc_group(pid_t pgid, int sig, com_proc_t *sender) {
    if (!IS_VALID_SIGNAL(sig)) {
        return EINVAL;
    }

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
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_spinlock_acquire(&curr_proc->signal_lock);
    int sig = com_sys_signal_check_nolock();

    if (-1 == sig) {
        goto end;
    }

    COM_SYS_SIGNAL_SIGMASK_UNSET(&curr_thread->pending_signals, sig);
    COM_SYS_SIGNAL_SIGMASK_UNSET(&curr_proc->pending_signals, sig);

    com_sigaction_t *sigaction = curr_proc->sigaction[sig];

    if (NULL == sigaction || SIG_DFL == sigaction->sa_action) {
        if (SA_KILL & SignalProperties[sig]) {
            KDEBUG("killing pid=%d because it received a deadly signal",
                   curr_proc->pid);
            com_spinlock_release(&curr_proc->signal_lock);
            curr_thread->lock_depth = 0;
            com_sys_proc_exit(curr_proc, sig);
            com_sys_sched_yield();
            __builtin_unreachable();
        } else if (SA_STOP & SignalProperties[sig]) {
            com_sys_proc_stop(curr_proc);
            com_sys_sched_yield();

            // After execution resumes
            // Execution gets beck here after the process receives a SIGCONT, as
            // can be seen in send_to_thread
            COM_SYS_SIGNAL_SIGMASK_UNSET(&curr_thread->pending_signals,
                                         SIGCONT);
            com_sys_proc_acquire_glock();
            curr_proc->stopped = false;
            com_sys_proc_release_glock();
            return;
        }
    }

    if (NULL == sigaction || SIG_IGN == sigaction->sa_action) {
        goto end;
    }

    com_sigframe_t *sframe;
    uintptr_t       stack;
    ARCH_CONTEXT_ALLOC_SIGFRAME(&sframe, &stack, ctx);
    ARCH_CONTEXT_SETUP_SIGFRAME(sframe, ctx);

    curr_thread->masked_signals |= sigaction->sa_mask.sig[0];

    if (!(SA_NODEFER & sigaction->sa_flags)) {
        COM_SYS_SIGNAL_SIGMASK_SET(&curr_thread->masked_signals, sig);
    }

    if (SA_RESETHAND & sigaction->sa_flags) {
        sigaction->sa_action = SIG_DFL;
    }

    ARCH_CONTEXT_SETUP_SIGRESTORE(sframe, &stack, sigaction->sa_restorer);
    ARCH_CONTEXT_SIGNAL_TRAMPOLINE(
        sframe, ctx, stack, sigaction->sa_action, sig);

end:
    com_spinlock_release(&curr_proc->signal_lock);
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

int com_sys_signal_check_nolock(void) {
    com_thread_t *curthread = ARCH_CPU_GET_THREAD();

    int           sig = -1;
    com_sigmask_t to_service =
        curthread->pending_signals & (~curthread->masked_signals);

    if (to_service) {
        sig = __builtin_ffsl(to_service);
    }

    return sig;
}

int com_sys_signal_check(void) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_spinlock_acquire(&curr_proc->signal_lock);
    int ret = com_sys_signal_check_nolock();
    com_spinlock_release(&curr_proc->signal_lock);

    return ret;
}
