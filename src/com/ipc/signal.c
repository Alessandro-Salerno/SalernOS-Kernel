#include <arch/context.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <kernel/platform/context.h>
#include <vendor/tailq.h>

#define IS_VALID_SIGNAL(sig) ((sig) > 0 && (sig) < NSIG)

// CREDIT: OpenBSD
#define SA_KILL     0x01 /* terminates process by default */
#define SA_CORE     0x02 /* ditto and coredumps */
#define SA_STOP     0x04 /* suspend process */
#define SA_TTYSTOP  0x08 /* ditto, from tty */
#define SA_IGNORE   0x10 /* ignore by default */
#define SA_CONT     0x20 /* continue if suspended */
#define SA_CANTMASK 0x40 /* non-maskable, catchable */

// CREDIT: vloxei64/ke
static const int g_signal_properties[] = {
    [SIGHUP] = SA_KILL,
    [SIGINT] = SA_KILL,
    [SIGQUIT] = SA_KILL | SA_CORE,
    [SIGILL] = SA_KILL | SA_CORE,
    [SIGTRAP] = SA_KILL | SA_CORE,
    [SIGABRT] = SA_KILL | SA_CORE,
    [SIGFPE] = SA_KILL | SA_CORE,
    [SIGKILL] = SA_KILL,
    [SIGBUS] = SA_KILL | SA_CORE,
    [SIGSEGV] = SA_KILL | SA_CORE,
    [SIGSYS] = SA_KILL | SA_CORE,
    [SIGPIPE] = SA_KILL,
    [SIGALRM] = SA_KILL,
    [SIGTERM] = SA_KILL,
    [SIGURG] = SA_IGNORE,
    [SIGSTOP] = SA_STOP,
    [SIGTSTP] = SA_STOP | SA_TTYSTOP,
    [SIGCONT] = SA_IGNORE | SA_CONT,
    [SIGCHLD] = SA_IGNORE,
    [SIGTTIN] = SA_STOP | SA_TTYSTOP,
    [SIGTTOU] = SA_STOP | SA_TTYSTOP,
    [SIGIO] = SA_IGNORE,
    [SIGXCPU] = SA_KILL,
    [SIGXFSZ] = SA_KILL,
    [SIGVTALRM] = SA_KILL,
    [SIGPROF] = SA_KILL,
    [SIGWINCH] = SA_IGNORE,
    [SIGUSR1] = SA_KILL,
    [SIGUSR2] = SA_KILL,

    // We don't support real-time signals, as they're a Linux feature and not
    // required by POSIX
    [SIGSYS + 1 ... NSIG - 1] = SA_KILL,
};

static SigMask signal_deliverable(Thread *thread, SigMask signals) {
    const SigMask kill = 1UL << (SIGKILL - 1);
    const SigMask stop = 1UL << (SIGSTOP - 1);
    const SigMask cont = 1UL << (SIGCONT - 1);
    SigMask pending = signals & (~thread->masked_signals | kill | stop);
    if (thread->signal_stopped) {
        pending &= kill;
        if (thread->proc->state != PROC_STATE_STOPPED) pending |= cont;
    }
    if (__atomic_load_n(&thread->exit_requested, __ATOMIC_ACQUIRE)) {
        pending |= kill;
    }
    return pending;
}

static SigMask signal_pending(Thread *thread) {
    return signal_deliverable(
        thread, thread->pending_signals | thread->proc->pending_signals);
}

void signal_kick(Thread *thread) {
    thread_lock(thread);
    spinlock_acq(&thread->proc->signal_lock);
    bool pending = signal_pending(thread) != 0;
    spinlock_rel(&thread->proc->signal_lock);
    if (pending) sched_interrupt_nolock(thread);
    thread_unlock(thread);
}

static bool signal_ignored(Proc *proc, int sig) {
    SignalAction *sigaction = proc->sigaction[sig];
    return (sigaction != NULL && sigaction->sa_action == SIG_IGN) ||
        ((sigaction == NULL || sigaction->sa_action == SIG_DFL) &&
         (g_signal_properties[sig] & SA_IGNORE));
}

static void signal_publish(Thread *thread, int sig) {
    // We drop signals that are ignored and not masked here, instead of causing
    // a spurious interrupt that then resolves to no action. This behavior is
    // applied only when the signal is not masked to stay consistent with the
    // requirement that later unmasks shall cause the signal to be dispatched.
    // We also process the signal anyway if it is a SIGCONT because it needs to
    // get to every thread in the process
    if (sig != SIGCONT && signal_ignored(thread->proc, sig) &&
        !signal_sigmask_isset(&thread->masked_signals, sig)) {
        return;
    }

    signal_sigmask_set(&thread->pending_signals, sig);
    if (sig == SIGCONT && thread->proc->state == PROC_STATE_STOPPED) {
        proc_transition_nolock(thread->proc, PROC_STATE_RUNNING, SIGCONT);
    }
}

static int send_to_proc(Proc *proc, int sig, Proc *sender) {
    (void)sender;
    if (proc == NULL) return ESRCH;
    spinlock_acq(&proc->threads_lock);
    spinlock_acq(&proc->signal_lock);
    if (proc->state == PROC_STATE_EXITED) {
        spinlock_rel(&proc->signal_lock);
        spinlock_rel(&proc->threads_lock);
        return ESRCH;
    }
    SigMask mask = 0;
    signal_sigmask_set(&mask, sig);
    bool found = false;
    Thread *thread;

    // As above, we have a special case for SIGCONT which needs to be delivered
    // to every thread. Other signals are delivered only to the first thread
    // that has them unmasked, or are parked in the process-wide signal mask
    TAILQ_FOREACH(thread, &proc->threads, proc_threads) {
        if (sig != SIGCONT && !(signal_deliverable(thread, mask) & mask)) {
            continue;
        }
        signal_publish(thread, sig);
        spinlock_rel(&proc->signal_lock);
        signal_kick(thread);
        spinlock_acq(&proc->signal_lock);
        found = true;
        if (sig != SIGCONT) break;
    }

    if (!found) signal_sigmask_set(&proc->pending_signals, sig);

    spinlock_rel(&proc->signal_lock);
    spinlock_rel(&proc->threads_lock);
    return 0;
}

void signal_sigset_empty(SignalSet *set) { memset(set, 0, sizeof(SignalSet)); }

int signal_send_to_proc(pid_t pid, int sig, Proc *sender) {
    if (!IS_VALID_SIGNAL(sig)) return EINVAL;

    log_debug("sending signal %d to pid=%d", sig, pid);
    Proc *proc = proc_get_by_pid(pid);

    int ret = send_to_proc(proc, sig, sender);
    if (proc != NULL) proc_rel(proc);
    return ret;
}

int signal_send_to_proc_group(pid_t pgid, int sig, Proc *sender) {
    if (!IS_VALID_SIGNAL(sig)) return EINVAL;

    log_debug("seding signal %d to pgid=%d", sig, pgid);

    ProcGroup *group = proc_get_group_by_pgid(pgid);

    if (group == NULL) return ESRCH;

    spinlock_acq(&group->procs_lock);
    Proc *p, *_;
    TAILQ_FOREACH_SAFE(p, &group->procs, procs, _) {
        send_to_proc(p, sig, sender);
    }
    spinlock_rel(&group->procs_lock);

    return 0;
}

int signal_send_to_thread_nolock(struct Thread *thread, int sig,
                                 struct Proc *sender) {
    (void)sender;
    signal_publish(thread, sig);
    return 0;
}

int signal_send_to_thread(struct Thread *thread, int sig, Proc *sender) {
    spinlock_acq(&thread->proc->signal_lock);
    int ret = signal_send_to_thread_nolock(thread, sig, sender);
    spinlock_rel(&thread->proc->signal_lock);
    signal_kick(thread);
    if (sig == SIGCONT) sched_notify_all(&thread->proc->stop_waitlist);
    return ret;
}

void signal_dispatch(Context *ctx, Thread *thread) {
    Proc *proc = thread->proc;
    assert(thread->lock_depth == 0);

restart:
    if (__atomic_load_n(&thread->exit_requested, __ATOMIC_ACQUIRE)) {
        thread_exit(thread);
        __builtin_unreachable();
    }

    spinlock_acq(&proc->signal_lock);
    int sig = signal_check_nolock();

    if (sig == SIGNAL_NONE) goto end;

    log_debug("tid=%d received signal %d", thread->tid, sig);

    signal_sigmask_unset(&thread->pending_signals, sig);
    signal_sigmask_unset(&proc->pending_signals, sig);

    SignalAction *sigaction = proc->sigaction[sig];

    if (sigaction == NULL || sigaction->sa_action == SIG_DFL) {
        if (g_signal_properties[sig] & SA_KILL) {
            spinlock_rel(&proc->signal_lock);

            log_debug("fatal signal %d, pid=%d (%s), rip=%p, rsp=%p", sig,
                      proc->pid, proc->executable_path, (void *)ctx->rip,
                      (void *)ctx->rsp);
            proc_terminate(0, sig);

            // This should never be reached
            log_debug("tid=%d in pid=%d is somehow still alive", thread->tid,
                      proc->pid);
            assert(false);
            __builtin_unreachable();
        } else if (g_signal_properties[sig] & SA_STOP) {
            proc_stop_nolock(sig);
            signal_sigmask_unset(&thread->pending_signals, sig);
            thread->signal_stopped = true;
            while (proc->state == PROC_STATE_STOPPED &&
                   signal_check_nolock() != SIGKILL) {
                spinlock_rel(&proc->signal_lock);
                sched_wait_nodrop(&proc->stop_waitlist);
                spinlock_acq(&proc->signal_lock);
            }
            thread->signal_stopped = false;
            signal_sigmask_unset(&thread->pending_signals, SIGCONT);
            spinlock_rel(&proc->signal_lock);
            goto restart;
        } else if (g_signal_properties[sig] & SA_IGNORE) {
            goto end;
        }
    }

    if (sigaction == NULL || sigaction->sa_action == SIG_IGN) goto end;

    void (*handler)(int) = sigaction->sa_action;
    void (*restorer)(void) = sigaction->sa_restorer;

    SigFrame kframe;
    memset(&kframe, 0, sizeof(kframe));
    kframe.info.si_signo = sig;
    kframe.uc.uc_sigmask.sig[0] = thread->masked_signals;

    thread->masked_signals |= sigaction->sa_mask.sig[0];

    if (!(sigaction->sa_flags & SA_NODEFER)) {
        signal_sigmask_set(&thread->masked_signals, sig);
    }

    if (sigaction->sa_flags & SA_RESETHAND) sigaction->sa_action = SIG_DFL;

    spinlock_rel(&proc->signal_lock);

    log_debug("pid=%d has restorer at %p for signal %d", proc->pid, restorer,
              sig);

    SigFrame *sframe;
    uptr stack;
    if (context_alloc_sigframe(&stack, ctx, &sframe) != 0) goto bad_frame;
    if (context_setup_sigframe(&kframe, sframe, ctx) != 0 ||
        memcpy_to_user(sframe, &kframe, sizeof(kframe)) != 0) {
        goto bad_frame;
    }

    if (context_setup_sigrestore(&stack, restorer) != 0) goto bad_frame;
    context_signal_trampoline(sframe, ctx, stack, handler, sig);
    return;

end:
    spinlock_rel(&proc->signal_lock);
    return;

bad_frame:
    // Not a signal: cannot be caught or blocked, so it cannot recurse
    log_debug("bad signal frame, pid=%d (%s), rsp=%p", proc->pid,
              proc->executable_path, (void *)ctx->rsp);
    proc_terminate(0, SIGSEGV);
}

int signal_set_mask_nolock(SigMask *mask, int how, SignalSet *set,
                           SignalSet *oset) {
    SigMask old_mask = *mask;

    if (set == NULL) goto noset;

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
    if (oset != NULL) {
        signal_sigset_empty(oset);
        oset->sig[0] = old_mask;
    }

    return 0;
}

int signal_set_mask(SigMask *mask, int how, SignalSet *set, SignalSet *oset,
                    Spinlock *signal_lock) {
    spinlock_acq(signal_lock);
    int ret = signal_set_mask_nolock(mask, how, set, oset);
    spinlock_rel(signal_lock);
    return ret;
}

int signal_check_nolock(void) {
    Thread *thread = cpu_get_thread();
    SigMask pending = signal_pending(thread);
    if (signal_sigmask_isset(&pending, SIGKILL)) return SIGKILL;
    return pending ? __builtin_ffsl(pending) : SIGNAL_NONE;
}

int signal_check(void) {
    Thread *thread = cpu_get_thread();
    if (thread->proc == NULL) return SIGNAL_NONE;
    spinlock_acq(&thread->proc->signal_lock);
    int ret = signal_check_nolock();
    spinlock_rel(&thread->proc->signal_lock);
    return ret;
}
