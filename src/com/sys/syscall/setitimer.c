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

#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <lib/util.h>
#include <stdint.h>
#include <sys/time.h>

struct itimer_callout_arg {
    com_thread_t *thread;
    bool          cancelled;
};

// NOTE: we never null the entire timer with {0} because that breaks the
// lock!
static inline void zero_timer_struct(com_thread_timer_t *timer) {
    timer->itimer_arg = NULL;
    timer->itimerval  = (struct itimerval){0};
    timer->ctime      = 0;
}

static inline uintmax_t timeval_to_ns(struct timeval t) {
    return t.tv_sec * KNANOS_PER_SEC + t.tv_usec * 1000UL;
}

static inline struct timeval timeval_from_ns(uintmax_t ns) {
    return (struct timeval){.tv_sec  = ns / KNANOS_PER_SEC,
                            .tv_usec = (ns % KNANOS_PER_SEC) / 1000};
}

static inline bool timeval_is_canonical(struct timeval t) {
    return t.tv_sec >= 0 && t.tv_usec >= 0 &&
           (uintmax_t)t.tv_usec < KMICROS_PER_SEC;
}

static void itimer_callout(com_callout_t *callout) {
    struct itimer_callout_arg *arg       = callout->arg;
    com_thread_t              *thread    = arg->thread;
    struct itimerval          *itimerval = &thread->real_timer.itimerval;
    uintmax_t                  new_delay = 0;
    uintmax_t                  now       = com_sys_callout_get_time();
    kspinlock_acquire(&thread->real_timer.lock);

    if (arg->cancelled) {
        kspinlock_release(&thread->real_timer.lock);
        com_mm_slab_free(arg, sizeof(struct itimer_callout_arg));
        return;
    }

    // POSIX: this lets us cancel the timer after we send SIGALRM one last time
    if (0 != timeval_to_ns(itimerval->it_interval)) {
        itimerval->it_value      = itimerval->it_interval;
        thread->real_timer.ctime = now;
        new_delay                = timeval_to_ns(itimerval->it_value);
    } else {
        zero_timer_struct(&thread->real_timer);
    }

    kspinlock_release(&thread->real_timer.lock);

    if (0 != new_delay) {
        com_sys_callout_reschedule(callout, new_delay);
    }

    com_ipc_signal_send_to_thread(thread, SIGALRM, thread->proc);
}

// SYSCALL: setitimer(int which, struct itimerval *new_val, struct itimerval
// *old_val)
COM_SYS_SYSCALL(com_sys_syscall_setitimer) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int               which   = COM_SYS_SYSCALL_ARG(int, 1);
    struct itimerval *new_val = COM_SYS_SYSCALL_ARG(void *, 2);
    struct itimerval *old_val = COM_SYS_SYSCALL_ARG(void *, 3);

    com_thread_t     *curr_thread = ARCH_CPU_GET_THREAD();
    com_syscall_ret_t ret         = COM_SYS_SYSCALL_OK(0);

    // TODO: support other timer types
    KASSERT(ITIMER_REAL == which);

    uintmax_t                  now              = com_sys_callout_get_time();
    uintmax_t                  new_delay        = 0;
    bool                       schedule_callout = false;
    struct itimer_callout_arg *callout_arg      = NULL;

    // We allocate here to avoid allocating under spinlocks
    if (NULL != new_val) {
        if (!timeval_is_canonical(new_val->it_value) ||
            !timeval_is_canonical(new_val->it_interval)) {
            return COM_SYS_SYSCALL_ERR(EINVAL);
        }

        new_delay = timeval_to_ns(new_val->it_value);
        if (0 != new_delay) {
            callout_arg = com_mm_slab_alloc(sizeof(struct itimer_callout_arg));
        }
    }

    kspinlock_acquire(&curr_thread->real_timer.lock);

    if (NULL != old_val) {
        // We can only be in one of these cases:
        //      1. This system call was never called before on this thread
        //      2. Otherwise, by definition, if the timer was set by a previous
        //      call, then it_value is strictly positive, since it is also
        //      updated by the callout handler above
        uintmax_t it_value = timeval_to_ns(
            curr_thread->real_timer.itimerval.it_value);
        uintmax_t elapsed = now - curr_thread->real_timer.ctime;
        uintmax_t rem     = 0;
        // We clamp this here because while in theory it is safe to do, in
        // practice, the real callout subsystem time may be ahead of the timer.
        // In this cae, the timer is de-facto expired, so we should say 0 and
        // not underflow
        if (elapsed < it_value) {
            rem = it_value - elapsed;
        }
        old_val->it_value    = timeval_from_ns(rem);
        old_val->it_interval = curr_thread->real_timer.itimerval.it_interval;
    }

    if (NULL != new_val) {
        if (NULL != curr_thread->real_timer.itimer_arg) {
            struct itimer_callout_arg *old_arg = curr_thread->real_timer
                                                     .itimer_arg;
            old_arg->cancelled = true;
        }

        if (0 == new_delay) {
            zero_timer_struct(&curr_thread->real_timer);
            goto end;
        }

        callout_arg->thread                = curr_thread;
        curr_thread->real_timer.ctime      = now;
        curr_thread->real_timer.itimerval  = *new_val;
        curr_thread->real_timer.itimer_arg = callout_arg;
        schedule_callout                   = true;
    }

end:
    kspinlock_release(&curr_thread->real_timer.lock);

    if (schedule_callout) {
        com_sys_callout_add(itimer_callout, callout_arg, new_delay);
    }

    return ret;
}
