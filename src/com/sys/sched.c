#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/info.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/ds/hashmap.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
#include <vendor/tailq.h>

static void sched_idle(void) {
    Thread *curr_thread = cpu_get_thread();

    for (;;) {
        curr_thread->lock_depth = 0;
        cpu_enable_interrupts();
        cpu_halt();
    }
}

static inline void sched_switch_vmm_context(Thread *curr, Thread *next) {
    VMMContext *prev_vmm_ctx = NULL;
    VMMContext *next_vmm_ctx = NULL;

    if (next->proc != NULL) {
        __atomic_add_fetch(&next->proc->num_running_threads, 1,
                           __ATOMIC_RELEASE);
        next_vmm_ctx = next->proc->vmm_context;
    }

    if (curr->proc != NULL) {
        __atomic_add_fetch(&curr->proc->num_running_threads, -1,
                           __ATOMIC_RELEASE);
        prev_vmm_ctx = curr->proc->vmm_context;
    }

    if (!context_is_user(&next->ctx)) {
        vmm_switch(NULL);
    } else if (next_vmm_ctx != NULL && next_vmm_ctx != prev_vmm_ctx) {
        vmm_switch(next_vmm_ctx);
    }
}

// Caller holds thread authority and the waitlist lock. These helpers only
// change membership and callers supply the state transition (if any)
static void sched_waitlist_insert(Waitlist *waitlist, Thread *thread,
                                  bool first) {
    assert(thread->waiting_on == NULL);
    thread->waiting_on = waitlist;
    if (first) TAILQ_INSERT_HEAD(&waitlist->queue, thread, threads);
    else TAILQ_INSERT_TAIL(&waitlist->queue, thread, threads);
    waitlist->count++;
}

static void sched_waitlist_remove(Thread *thread) {
    Waitlist *waitlist = thread->waiting_on;
    assert(waitlist != NULL);
    TAILQ_REMOVE(&waitlist->queue, thread, threads);
    waitlist->count--;
    thread->waiting_on = NULL;
}

static void sched_handle_timeout(Callout *callout) {
    Thread *thread = callout->arg;
    thread_lock(thread);
    if (callout->value == thread->epoch && thread->waiting_on != NULL) {
        thread->wait_error = ETIMEDOUT;
        sched_notify_thread_nolock(thread);
    }
    thread_unlock(thread);
    thread_rel(thread);
}

void sched_switch_finish(ContextMessage message) {
    // Keep completion non-preemptible, then restore the incoming lock depth
    spinlock_fake_acq();
    spinlock_rel(&cpu_get()->runqueue_lock);
    if (message.callback != NULL) {
        message.callback(message.arg, message.value, true);
    }
    spinlock_fake_rel();
}

int sched_yield_nolock(bool interruptible, ContextMessage message) {
    ProfilerData profiler_data = profiler_start_function(
        PROFILE_FUNC_SCHED_YIELD);
    Cpu *cpu = cpu_get();
    Thread *curr = cpu->thread;
    assert(curr == NULL || spinlock_is_held(curr->sched_lock));
    assert(curr == NULL || curr->lock_depth == 1 || curr->lock_depth == 2);
    Thread *next = TAILQ_FIRST(&cpu->sched_queue);

    if (curr == NULL) {
        profiler_end_function(&profiler_data);
        return 0;
    }

    if (interruptible && signal_check() != SIGNAL_NONE) {
        profiler_end_function(&profiler_data);
        return EINTR;
    }

    if (next == NULL) {
        if (__atomic_load_n(&curr->state, __ATOMIC_RELAXED) ==
            THREAD_STATE_RUNNING) {
            profiler_end_function(&profiler_data);
            sched_switch_finish(message);
            return 0;
        }

        next = cpu->idle_thread;
    } else {
        assert(next != cpu->idle_thread);
        TAILQ_REMOVE(&cpu->sched_queue, next, threads);
    }

    if (curr == next) {
        log_urgent("curr = %p, next = %p | curr->tid = %d, next->tid = %d | "
                   "IS_USER(curr) = %d, "
                   "IS_USER(next) = %d",
                   curr, next, curr->tid, next->tid,
                   context_is_user(&curr->ctx), context_is_user(&next->ctx));
        assert(curr != next);
    }

    if (__atomic_load_n(&curr->state, __ATOMIC_RELAXED) ==
        THREAD_STATE_RUNNING) {
        thread_transition_nolock(curr, THREAD_STATE_READY);
        if (curr != cpu->idle_thread) {
            TAILQ_INSERT_TAIL(&cpu->sched_queue, curr, threads);
        }
    }
    assert(__atomic_load_n(&curr->state, __ATOMIC_RELAXED) !=
           THREAD_STATE_RUNNING);

    curr->cpu = NULL;
    curr->last_cpu = cpu;

    sched_switch_vmm_context(curr, next);

    CONTEXT_SAVE_EXTRA(curr->xctx);
    if (context_is_user(&next->ctx)) {
        CONTEXT_RESTORE_EXTRA(next->xctx);
    }

    profiler_end_function(&profiler_data);

    cpu->thread = next;
    next->cpu = cpu;
    thread_transition_nolock(next, THREAD_STATE_RUNNING);

    // Restore kernel stack
    cpu_set_interrupt_stack(cpu, next->kernel_stack);

    // Switch threads
    // NOTE: this doesn't jump to userspace directly! Instead, this switches the
    //        stack pointer and other registers so that when returning from the
    //        underlying assembly function, control is handed to whatever
    //        address is on the stack, which is most likely this function. Once
    //        the execution gets back here, sched_yield can return to
    //        its caller, which can be a hardware timer or anything else (e.g.,
    //        wait). Remember that, since the stack has changed, there's no
    //        guarantee that this function will return to the same point from
    //        which it was called, in fact, quite the opposite.
    assert(next->sched_lock == &cpu->runqueue_lock);
#if CONFIG_TRACK_SLEEP_TIME
    u64 sleep_start = cpu_get_timestamp();
#endif
    ContextMessage incoming = ctx_switch(
        &next->ctx, &curr->ctx, message.callback, message.arg, message.value);
#if CONFIG_TRACK_SLEEP_TIME
    // NOTE: this goes here, otherwise it may sample after interrupts are turned
    // back on and the value may be wrong due to nested preemption
    u64 sleep_end = cpu_get_timestamp();
#endif
    sched_switch_finish(incoming);
#if CONFIG_TRACK_SLEEP_TIME
    u64 time_slept = sleep_end - sleep_start;
    __atomic_add_fetch(&curr->time_slept, time_slept, __ATOMIC_RELAXED);
#endif
    return 0;
}

void sched_yield(void) {
    thread_reap();
    Thread *curr_thread = cpu_get_thread();
    if (curr_thread != NULL) thread_lock(curr_thread);
    sched_yield_nolock(false, (ContextMessage){0});
    // lock released elsewhere
}

void sched_isr(InterruptHandler *isr, Context *ctx) {
    (void)isr;
    (void)ctx;
    sched_yield();
}

int sched_wait(Waitlist *waitlist, u64 timeout, bool interruptible,
               const SchedWaitOps *ops, void *primitive) {
    Thread *curr = cpu_get_thread();
    assert(curr != cpu_get()->idle_thread);

    if (ops != NULL && ops->prepare != NULL) ops->prepare(primitive);

    thread_lock(curr);
    curr->wait_interruptible = interruptible;
    curr->wait_error = 0;

    spinlock_acq(&waitlist->lock);
    sched_waitlist_insert(waitlist, curr, false);
    thread_transition_nolock(curr, THREAD_STATE_WAITING);
    spinlock_rel(&waitlist->lock);

    if (timeout) {
        // The firing owns this reference: a detached timeout may outlive
        // this wait and even the thread's eventual exit
        thread_hold(curr);
        curr->timed_wait_callout->value = curr->epoch;
        callout_set_and_enqueue(curr->timed_wait_callout, sched_handle_timeout,
                                timeout);
    }

    ContextMessage message = {0};
    if (ops != NULL) message = ops->release(primitive);

    int ret = sched_yield_nolock(interruptible, message);
    if (ret != 0) {
        // The switch did not consume the message or release authority
        spinlock_acq(&waitlist->lock);
        sched_waitlist_remove(curr);
        thread_transition_nolock(curr, THREAD_STATE_RUNNING);
        spinlock_rel(&waitlist->lock);
        thread_unlock(curr);
        if (message.callback != NULL) {
            message.callback(message.arg, message.value, false);
        }
    }

    if (timeout && callout_cancel(curr->timed_wait_callout)) thread_rel(curr);
    if (ret == 0) ret = curr->wait_error;

    thread_reap();

    if (ops != NULL) ops->acquire(primitive);
    return ret;
}

void sched_wait_nodrop(Waitlist *waitlist) {
    sched_wait(waitlist, 0, true, NULL, NULL);
}

int sched_wait_nodrop_timeout(Waitlist *waitlist, u64 timeout) {
    return sched_wait(waitlist, timeout, true, NULL, NULL);
}

void sched_notify_n(Waitlist *waitlist, usize num_waiters) {
    while (num_waiters > 0) {
        spinlock_acq(&waitlist->lock);
        Thread *thread = TAILQ_FIRST(&waitlist->queue);
        if (thread == NULL) {
            spinlock_rel(&waitlist->lock);
            return;
        }
        // Only retain the immortal lock pointer across the unlock. Neither
        // the thread nor the sampled head needs to survive this slow path
        Spinlock *lock = __atomic_load_n(&thread->sched_lock, __ATOMIC_ACQUIRE);
        spinlock_rel(&waitlist->lock);
        spinlock_acq(lock);
        spinlock_acq(&waitlist->lock);
        thread = TAILQ_FIRST(&waitlist->queue);
        if (thread != NULL &&
            lock == __atomic_load_n(&thread->sched_lock, __ATOMIC_ACQUIRE)) {
            sched_waitlist_remove(thread);
            thread_ready_nolock(thread);
            num_waiters--;
        }
        spinlock_rel(&waitlist->lock);
        spinlock_rel(lock);
    }
}

void sched_notify(Waitlist *waitlist) { sched_notify_n(waitlist, 1); }

void sched_notify_all(Waitlist *waitlist) {
    spinlock_acq(&waitlist->lock);
    usize count = waitlist->count;
    spinlock_rel(&waitlist->lock);
    sched_notify_n(waitlist, count);
}

void sched_notify_thread_nolock(Thread *thread) {
    Waitlist *waitlist = thread->waiting_on;
    if (waitlist != NULL) {
        spinlock_acq(&waitlist->lock);
        sched_waitlist_remove(thread);
        thread_ready_nolock(thread);
        spinlock_rel(&waitlist->lock);
    }
}

// All asynchronous interruption, including termination, obeys wait policy
void sched_interrupt_nolock(Thread *thread) {
    ThreadState state = __atomic_load_n(&thread->state, __ATOMIC_RELAXED);
    if (state == THREAD_STATE_WAITING && thread->wait_interruptible) {
        thread->wait_error = EINTR;
        sched_notify_thread_nolock(thread);
    } else if (state == THREAD_STATE_READY) {
        sched_prioritize_nolock(thread);
    } else if (state == THREAD_STATE_RUNNING && thread->cpu != cpu_get()) {
        cpu_send_ipi(thread->cpu, CPU_IPI_SIGNAL);
    }
}

void sched_notify_thread(Thread *thread) {
    thread_lock(thread);
    sched_notify_thread_nolock(thread);
    thread_unlock(thread);
}

void sched_prioritize(Thread *thread) {
    thread_lock(thread);
    sched_prioritize_nolock(thread);
    thread_unlock(thread);
}

void sched_prioritize_nolock(Thread *thread) {
    ThreadState state = __atomic_load_n(&thread->state, __ATOMIC_RELAXED);
    if (state == THREAD_STATE_WAITING) {
        Waitlist *waitlist = thread->waiting_on;
        spinlock_acq(&waitlist->lock);
        sched_waitlist_remove(thread);
        sched_waitlist_insert(waitlist, thread, true);
        spinlock_rel(&waitlist->lock);
    } else if (state == THREAD_STATE_READY) {
        Cpu *cpu = thread->last_cpu;
        if (thread != cpu->idle_thread) {
            TAILQ_REMOVE(&cpu->sched_queue, thread, threads);
            TAILQ_INSERT_HEAD(&cpu->sched_queue, thread, threads);
        }
    }
    // Reordering does not end a wait: keep its epoch so pending timeout and
    // deferred-notification messages remain valid
}

void sched_init(void) {
    Cpu *curr_cpu = cpu_get();
    SLIST_INIT(&curr_cpu->zombies);
    curr_cpu->idle_thread = thread_new_kernel(NULL, sched_idle, NULL);
    curr_cpu->idle_thread->cpu = curr_cpu;
    // NOTE: this is different from other CPUs because those run the idle thread
    // as their first thread, while the BSP runs the init thread as its first
    // thread
    thread_transition(curr_cpu->idle_thread, THREAD_STATE_READY);
}
