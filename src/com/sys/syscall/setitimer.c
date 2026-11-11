#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <sys/time.h>

struct ITimerCalloutArg {
    Thread *thread;
    bool cancelled;
};

// NOTE: we never null the entire timer with {0} because that breaks the
// lock!
static inline void zero_timer_struct(ThreadTimer *timer) {
    timer->itimer_arg = NULL;
    timer->itimerval = (struct itimerval){0};
    timer->ctime = 0;
}

static inline u64 timeval_to_ns(struct timeval t) {
    return t.tv_sec * NANOS_PER_SEC + t.tv_usec * 1000UL;
}

static inline struct timeval timeval_from_ns(u64 ns) {
    return (struct timeval){.tv_sec = ns / NANOS_PER_SEC,
                            .tv_usec = (ns % NANOS_PER_SEC) / 1000};
}

static inline bool timeval_is_canonical(struct timeval t) {
    return t.tv_sec >= 0 && t.tv_usec >= 0 && (u64)t.tv_usec < MICROS_PER_SEC;
}

static void itimer_callout(Callout *callout) {
    struct ITimerCalloutArg *arg = callout->arg;
    Thread *thread = arg->thread;
    struct itimerval *itimerval = &thread->real_timer.itimerval;
    u64 new_delay = 0;
    u64 now = callout_get_time();
    spinlock_acq(&thread->real_timer.lock);

    if (arg->cancelled ||
        __atomic_load_n(&thread->exit_requested, __ATOMIC_ACQUIRE)) {
        if (thread->real_timer.itimer_arg == arg) {
            zero_timer_struct(&thread->real_timer);
        }
        spinlock_rel(&thread->real_timer.lock);
        slab_free(arg, sizeof(struct ITimerCalloutArg));
        thread_rel(thread);
        return;
    }

    // POSIX: this lets us cancel the timer after we send SIGALRM one last time
    if (timeval_to_ns(itimerval->it_interval) != 0) {
        itimerval->it_value = itimerval->it_interval;
        thread->real_timer.ctime = now;
        new_delay = timeval_to_ns(itimerval->it_value);
    } else {
        zero_timer_struct(&thread->real_timer);
    }

    spinlock_rel(&thread->real_timer.lock);

    if (new_delay != 0) callout_reschedule(callout, new_delay);

    signal_send_to_thread(thread, SIGALRM, thread->proc);
    if (new_delay == 0) {
        slab_free(arg, sizeof(struct ITimerCalloutArg));
        thread_rel(thread);
    }
}

// SYSCALL: setitimer(int which, struct itimerval *new_val, struct itimerval
// *old_val)
SYSCALL(syscall_setitimer) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int which = SYSCALL_ARG(int, 1);
    struct itimerval *new_val = SYSCALL_ARG(void *, 2);
    struct itimerval *old_val = SYSCALL_ARG(void *, 3);

    Thread *curr_thread = cpu_get_thread();
    SyscallRet ret = SYSCALL_OK(0);

    // TODO: support other timer types
    assert(which == ITIMER_REAL);

    u64 now = callout_get_time();
    u64 new_delay = 0;
    bool schedule_callout = false;
    struct ITimerCalloutArg *callout_arg = NULL;

    struct itimerval knew;
    struct itimerval kold;

    // We allocate here to avoid allocating under spinlocks
    if (new_val != NULL) {
        int copy_ret = memcpy_from_user(&knew, new_val, sizeof(knew));
        if (copy_ret != 0) return SYSCALL_ERR(copy_ret);
        if (!timeval_is_canonical(knew.it_value) ||
            !timeval_is_canonical(knew.it_interval)) {
            return SYSCALL_ERR(EINVAL);
        }

        new_delay = timeval_to_ns(knew.it_value);
        if (new_delay != 0) {
            callout_arg = slab_alloc(sizeof(struct ITimerCalloutArg));
        }
    }

    spinlock_acq(&curr_thread->real_timer.lock);

    if (old_val != NULL) {
        // We can only be in one of these cases:
        //      1. This system call was never called before on this thread
        //      2. Otherwise, by definition, if the timer was set by a previous
        //      call, then it_value is strictly positive, since it is also
        //      updated by the callout handler above
        u64 it_value = timeval_to_ns(
            curr_thread->real_timer.itimerval.it_value);
        u64 elapsed = now - curr_thread->real_timer.ctime;
        u64 rem = 0;
        // We clamp this here because while in theory it is safe to do, in
        // practice, the real callout subsystem time may be ahead of the timer.
        // In this cae, the timer is de-facto expired, so we should say 0 and
        // not underflow
        if (elapsed < it_value) rem = it_value - elapsed;
        kold.it_value = timeval_from_ns(rem);
        kold.it_interval = curr_thread->real_timer.itimerval.it_interval;
    }

    if (new_val != NULL) {
        if (curr_thread->real_timer.itimer_arg != NULL) {
            struct ITimerCalloutArg *old_arg = curr_thread->real_timer
                                                   .itimer_arg;
            old_arg->cancelled = true;
        }

        if (new_delay == 0) {
            zero_timer_struct(&curr_thread->real_timer);
            goto end;
        }

        callout_arg->thread = curr_thread;
        curr_thread->real_timer.ctime = now;
        curr_thread->real_timer.itimerval = knew;
        curr_thread->real_timer.itimer_arg = callout_arg;
        schedule_callout = true;
    }

end:
    spinlock_rel(&curr_thread->real_timer.lock);

    // The new timer is armed even if the old one cannot be reported
    if (old_val != NULL && memcpy_to_user(old_val, &kold, sizeof(kold)) != 0) {
        ret = SYSCALL_ERR(EFAULT);
    }

    if (schedule_callout) {
        thread_hold(curr_thread);
        callout_add(itimer_callout, callout_arg, new_delay);
    }

    return ret;
}
