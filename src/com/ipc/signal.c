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

#include <arch/context.h>
#include <errno.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/sched.h>
#include <kernel/platform/context.h>
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

#define TKILL_STATUS_MASKED -1
#define TKILL_STATUS_BAD    -2

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
    COM_IPC_SIGNAL_SIGMASK_SET(&thread->pending_signals, sig);
    KDEBUG("sending signal %d to tid=%d", sig, thread->tid);

    if (COM_IPC_SIGNAL_SIGMASK_ISSET(&thread->masked_signals, sig)) {
        return TKILL_STATUS_MASKED;
    }

    if (COM_IPC_SIGNAL_NONE != thread->proc->stop_signal && SIGCONT != sig) {
        return TKILL_STATUS_BAD;
    }

    kspinlock_acquire(&thread->sched_lock);
    if (NULL != thread->waiting_on) {
        kspinlock_t *wait_cond = thread->waiting_cond;
        kspinlock_acquire(wait_cond);
        com_sys_sched_notify_thread_nolock(thread);
        kspinlock_release(wait_cond);
    } else if (!thread->runnable) {
        com_sys_thread_ready_nolock(thread);
    } /*else if (NULL != thread->cpu) {
        KURGENT("signal ipi to thread %d on cpu %d",
                thread->tid,
                thread->cpu->id);
        ARCH_CPU_SEND_IPI(thread->cpu, ARCH_CPU_IPI_SIGNAL);
    }*/

    kspinlock_release(&thread->sched_lock);
    return 0;
}

void com_ipc_signal_sigset_emptY(com_sigset_t *set) {
    kmemset(set, sizeof(com_sigset_t), 0);
}

int com_ipc_signal_send_to_proc(pid_t pid, int sig, com_proc_t *sender) {
    (void)sender;
    // TODO: should I check permission to send the signal?

    if (!IS_VALID_SIGNAL(sig)) {
        return EINVAL;
    }

    KDEBUG("sending signal %d to pid=%d", sig, pid);
    com_proc_t *proc = com_sys_proc_get_by_pid(pid);

    if (NULL == proc) {
        return ESRCH;
    }

    kspinlock_acquire(&proc->signal_lock);
    if (proc->exited) {
        kspinlock_release(&proc->signal_lock);
        return ESRCH;
    }

    kspinlock_acquire(&proc->threads_lock);
    com_thread_t *t, *_;
    bool          found = false;
    TAILQ_FOREACH_SAFE(t, &proc->threads, proc_threads, _) {
        if (0 == send_to_thread(t, sig)) {
            found = true;
            break;
        }
    }

    if (!found) {
        COM_IPC_SIGNAL_SIGMASK_SET(&proc->pending_signals, sig);
    }

    kspinlock_release(&proc->threads_lock);
    kspinlock_release(&proc->signal_lock);

    return 0;
}

int com_ipc_signal_send_to_proc_group(pid_t pgid, int sig, com_proc_t *sender) {
    if (!IS_VALID_SIGNAL(sig)) {
        return EINVAL;
    }

    KDEBUG("seding signal %d to pgid=%d", sig, pgid);

    com_proc_group_t *group = com_sys_proc_get_group_by_pgid(pgid);

    if (NULL == group) {
        return ESRCH;
    }

    kspinlock_acquire(&group->procs_lock);
    com_proc_t *p, *_;
    TAILQ_FOREACH_SAFE(p, &group->procs, procs, _) {
        com_ipc_signal_send_to_proc(p->pid, sig, sender);
    }
    kspinlock_release(&group->procs_lock);

    return 0;
}

int com_ipc_signal_send_to_thread(struct com_thread *thread,
                                  int                sig,
                                  com_proc_t        *sender) {
    (void)sender;
    // TODO: (same as for proc) actually check that this can be done (sender
    // check)
    kspinlock_acquire(&thread->proc->signal_lock);
    int ret = send_to_thread(thread, sig);
    kspinlock_release(&thread->proc->signal_lock);
    if (TKILL_STATUS_MASKED == ret) {
        return 0;
    }
    return ret;
}

void com_ipc_signal_dispatch(arch_context_t *ctx, com_thread_t *thread) {
    com_proc_t *proc = thread->proc;

    kspinlock_acquire(&proc->signal_lock);
    int sig = com_ipc_signal_check_nolock();

    if (-1 == sig) {
        goto end;
    }

    KDEBUG("tid=%d received signal %d", thread->tid, sig);

    COM_IPC_SIGNAL_SIGMASK_UNSET(&thread->pending_signals, sig);

    com_sigaction_t *sigaction = proc->sigaction[sig];

    if (NULL == sigaction || SIG_DFL == sigaction->sa_action) {
        if (SA_KILL & SignalProperties[sig]) {
            KDEBUG("killing pid=%d because it received a deadly signal %d",
                   proc->pid,
                   sig);
            kspinlock_release(&proc->signal_lock);
            thread->lock_depth = 0;

            com_sys_proc_terminate(proc, sig);
            com_sys_sched_yield();

            // This should never be reached
            KDEBUG("tid=%d in pid=%d is somehow still alive",
                   thread->tid,
                   proc->pid);
            KASSERT(false);
            __builtin_unreachable();
        } else if (SA_STOP & SignalProperties[sig]) {
            kspinlock_release(&proc->signal_lock);

            // com_sys_proc_stop will return early if the stop signal has
            // already been sent to a single thread. This way we don't flood the
            // system with an infinite number of stop signals
            com_sys_proc_stop(proc, sig);

            // we clear the stop signal because com_sys_proc_stop may have sent
            // it to us again
            COM_IPC_SIGNAL_SIGMASK_UNSET(&thread->pending_signals, sig);

            // com_sys_proc_stop will ensure that the signal is sent to all
            // threads, so all will arrive here, and all will set themselves to
            // not runnable
            kspinlock_acquire(&thread->sched_lock);
            thread->runnable = false;
            com_sys_sched_yield_nolock();

            KASSERT(COM_IPC_SIGNAL_SIGMASK_ISSET(&thread->pending_signals,
                                                 SIGCONT));

            // After execution resumes
            // Execution gets beck here after the process receives a SIGCONT, as
            // can be seen in send_to_thread
            COM_IPC_SIGNAL_SIGMASK_UNSET(&thread->pending_signals, SIGCONT);
            com_sys_proc_acquire_glock();
            proc->stop_signal   = COM_IPC_SIGNAL_NONE;
            proc->stop_notified = false;
            com_sys_proc_release_glock();
            return;
        } else if (SA_IGNORE & SignalProperties[sig]) {
            goto end;
        }
    }

    if (NULL == sigaction || SIG_IGN == sigaction->sa_action) {
        goto end;
    }

    com_sigframe_t *sframe;
    uintptr_t       stack;
    arch_context_alloc_sigframe(&sframe, &stack, ctx);
    kmemset(sframe, sizeof(com_sigframe_t), 0);
    sframe->info.si_signo        = sig;
    sframe->uc.uc_sigmask.sig[0] = thread->masked_signals;
    arch_context_setup_sigframe(sframe, ctx);

    thread->masked_signals |= sigaction->sa_mask.sig[0];

    if (!(SA_NODEFER & sigaction->sa_flags)) {
        COM_IPC_SIGNAL_SIGMASK_SET(&thread->masked_signals, sig);
    }

    if (SA_RESETHAND & sigaction->sa_flags) {
        sigaction->sa_action = SIG_DFL;
    }

    KDEBUG("pid=%d has restorer at %p for signal %d",
           proc->pid,
           sigaction->sa_restorer,
           sig);
    arch_context_setup_sigrestore(&stack, sigaction->sa_restorer);
    arch_context_signal_trampoline(sframe,
                                   ctx,
                                   stack,
                                   sigaction->sa_action,
                                   sig);

end:
    kspinlock_release(&proc->signal_lock);
}

int com_ipc_signal_set_mask_nolock(com_sigmask_t *mask,
                                   int            how,
                                   com_sigset_t  *set,
                                   com_sigset_t  *oset) {
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
            *mask &= ~set->sig[0];
            break;
        default:
            return EINVAL;
    }

noset:
    if (NULL != oset) {
        com_ipc_signal_sigset_emptY(oset);
        oset->sig[0] = old_mask;
    }

    return 0;
}

int com_ipc_signal_set_mask(com_sigmask_t *mask,
                            int            how,
                            com_sigset_t  *set,
                            com_sigset_t  *oset,
                            kspinlock_t   *signal_lock) {
    kspinlock_acquire(signal_lock);
    int ret = com_ipc_signal_set_mask_nolock(mask, how, set, oset);
    kspinlock_release(signal_lock);
    return ret;
}

int com_ipc_signal_check_nolock(void) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();

    int           sig        = -1;
    com_sigmask_t to_service = curr_thread->pending_signals &
                               (~curr_thread->masked_signals);

    if (to_service) {
        sig = __builtin_ffsl(to_service);
    }

    return sig;
}

int com_ipc_signal_check(void) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    kspinlock_acquire(&curr_proc->signal_lock);
    int ret = com_ipc_signal_check_nolock();
    kspinlock_release(&curr_proc->signal_lock);

    return ret;
}
