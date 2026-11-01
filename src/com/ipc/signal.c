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

// Signal policy lives here. Scheduler callers ask only whether interruption
// is pending and uninterruptible waits never consult this predicate
static com_sigmask_t signal_pending(com_thread_t *thread) {
    const com_sigmask_t kill    = 1UL << (SIGKILL - 1);
    const com_sigmask_t stop    = 1UL << (SIGSTOP - 1);
    const com_sigmask_t cont    = 1UL << (SIGCONT - 1);
    com_sigmask_t       pending = thread->pending_signals &
                            (~thread->masked_signals | kill | stop);
    if (thread->signal_stopped) {
        pending &= kill;
        if (COM_IPC_SIGNAL_NONE == thread->proc->stop_signal) {
            pending |= cont;
        }
    }
    if (__atomic_load_n(&thread->exit_requested, __ATOMIC_ACQUIRE)) {
        pending |= kill;
    }
    return pending;
}

void com_ipc_signal_kick(com_thread_t *thread) {
    com_sys_thread_lock(thread);
    kspinlock_acquire(&thread->proc->signal_lock);
    bool pending = 0 != signal_pending(thread);
    kspinlock_release(&thread->proc->signal_lock);
    if (pending) {
        com_sys_sched_interrupt_nolock(thread);
    }
    com_sys_thread_unlock(thread);
}

static void signal_publish(com_thread_t *thread, int sig) {
    COM_IPC_SIGNAL_SIGMASK_SET(&thread->pending_signals, sig);
    if (SIGCONT == sig) {
        thread->proc->stop_signal   = COM_IPC_SIGNAL_NONE;
        thread->proc->stop_notified = false;
    }
}

static int send_to_proc(com_proc_t *proc, int sig, com_proc_t *sender) {
    (void)sender;
    if (NULL == proc) {
        return ESRCH;
    }
    kspinlock_acquire(&proc->threads_lock);
    kspinlock_acquire(&proc->signal_lock);
    if (proc->exited) {
        kspinlock_release(&proc->signal_lock);
        kspinlock_release(&proc->threads_lock);
        return ESRCH;
    }
    bool          found = false;
    com_thread_t *thread;
    TAILQ_FOREACH(thread, &proc->threads, proc_threads) {
        signal_publish(thread, sig);
        com_sigmask_t pending  = signal_pending(thread);
        bool          eligible = COM_IPC_SIGNAL_SIGMASK_ISSET(&pending, sig);
        kspinlock_release(&proc->signal_lock);
        com_ipc_signal_kick(thread);
        kspinlock_acquire(&proc->signal_lock);
        found |= eligible;
        if (eligible && SIGCONT != sig) {
            break;
        }
    }
    if (!found) {
        COM_IPC_SIGNAL_SIGMASK_SET(&proc->pending_signals, sig);
    }
    kspinlock_release(&proc->signal_lock);
    kspinlock_release(&proc->threads_lock);
    return 0;
}

void com_ipc_signal_sigset_emptY(com_sigset_t *set) {
    kmemset(set, sizeof(com_sigset_t), 0);
}

int com_ipc_signal_send_to_proc(pid_t pid, int sig, com_proc_t *sender) {
    if (!IS_VALID_SIGNAL(sig)) {
        return EINVAL;
    }

    KDEBUG("sending signal %d to pid=%d", sig, pid);
    com_proc_t *proc = com_sys_proc_get_by_pid(pid);

    return send_to_proc(proc, sig, sender);
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
        send_to_proc(p, sig, sender);
    }
    kspinlock_release(&group->procs_lock);

    return 0;
}

int com_ipc_signal_send_to_thread_nolock(struct com_thread *thread,
                                         int                sig,
                                         struct com_proc   *sender) {
    (void)sender;
    signal_publish(thread, sig);
    return 0;
}

int com_ipc_signal_send_to_thread(struct com_thread *thread,
                                  int                sig,
                                  com_proc_t        *sender) {
    kspinlock_acquire(&thread->proc->signal_lock);
    int ret = com_ipc_signal_send_to_thread_nolock(thread, sig, sender);
    kspinlock_release(&thread->proc->signal_lock);
    com_ipc_signal_kick(thread);
    if (SIGCONT == sig) {
        com_sys_sched_notify_all(&thread->proc->stop_waitlist);
    }
    return ret;
}

void com_ipc_signal_dispatch(arch_context_t *ctx, com_thread_t *thread) {
    com_proc_t *proc = thread->proc;

restart:
    if (__atomic_load_n(&thread->exit_requested, __ATOMIC_ACQUIRE)) {
        thread->lock_depth = 0;
        com_sys_thread_exit(thread);
        __builtin_unreachable();
    }
    kspinlock_acquire(&proc->signal_lock);
    int sig = com_ipc_signal_check_nolock();

    if (COM_IPC_SIGNAL_NONE == sig) {
        goto end;
    }

    KDEBUG("tid=%d received signal %d", thread->tid, sig);

    COM_IPC_SIGNAL_SIGMASK_UNSET(&thread->pending_signals, sig);

    com_sigaction_t *sigaction = proc->sigaction[sig];

    if (NULL == sigaction || SIG_DFL == sigaction->sa_action) {
        if (SA_KILL & SignalProperties[sig]) {
            proc->stop_signal = sig;
            kspinlock_release(&proc->signal_lock);
            thread->lock_depth = 0;

            KDEBUG("fatal signal %d, pid=%d (%s), rip=%p, rsp=%p",
                   sig,
                   proc->pid,
                   proc->executable_path,
                   (void *)ctx->rip,
                   (void *)ctx->rsp);
            com_sys_proc_terminate(0);

            // This should never be reached
            KDEBUG("tid=%d in pid=%d is somehow still alive",
                   thread->tid,
                   proc->pid);
            KASSERT(false);
            __builtin_unreachable();
        } else if (SA_STOP & SignalProperties[sig]) {
            com_sys_proc_stop_nolock(sig);
            COM_IPC_SIGNAL_SIGMASK_UNSET(&thread->pending_signals, sig);
            thread->signal_stopped = true;
            while (COM_IPC_SIGNAL_NONE != proc->stop_signal &&
                   SIGKILL != com_ipc_signal_check_nolock()) {
                kspinlock_release(&proc->signal_lock);
                int depth          = thread->lock_depth;
                thread->lock_depth = 0;
                com_sys_sched_wait_nodrop(&proc->stop_waitlist);
                thread->lock_depth = depth;
                kspinlock_acquire(&proc->signal_lock);
            }
            thread->signal_stopped = false;
            COM_IPC_SIGNAL_SIGMASK_UNSET(&thread->pending_signals, SIGCONT);
            kspinlock_release(&proc->signal_lock);
            goto restart;
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
    com_thread_t *thread  = ARCH_CPU_GET_THREAD();
    com_sigmask_t pending = signal_pending(thread);
    if (COM_IPC_SIGNAL_SIGMASK_ISSET(&pending, SIGKILL)) {
        return SIGKILL;
    }
    return pending ? __builtin_ffsl(pending) : COM_IPC_SIGNAL_NONE;
}

int com_ipc_signal_check(void) {
    com_thread_t *thread = ARCH_CPU_GET_THREAD();
    if (NULL == thread->proc) {
        return COM_IPC_SIGNAL_NONE;
    }
    kspinlock_acquire(&thread->proc->signal_lock);
    int ret = com_ipc_signal_check_nolock();
    kspinlock_release(&thread->proc->signal_lock);
    return ret;
}
