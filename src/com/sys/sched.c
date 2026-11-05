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
#include <arch/cpu.h>
#include <arch/info.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/mmu.h>
#include <lib/hashmap.h>
#include <lib/spinlock.h>
#include <lib/util.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

static void sched_idle(void) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();

    for (;;) {
        curr_thread->lock_depth = 0;
        ARCH_CPU_ENABLE_INTERRUPTS();
        ARCH_CPU_HALT();
    }
}

static inline void sched_switch_vmm_context(com_thread_t *curr,
                                            com_thread_t *next) {
    com_vmm_context_t *prev_vmm_ctx = NULL;
    com_vmm_context_t *next_vmm_ctx = NULL;

    if (NULL != next->proc) {
        __atomic_add_fetch(&next->proc->num_running_threads,
                           1,
                           __ATOMIC_RELEASE);
        next_vmm_ctx = next->proc->vmm_context;
    }

    if (NULL != curr->proc) {
        __atomic_add_fetch(&curr->proc->num_running_threads,
                           -1,
                           __ATOMIC_RELEASE);
        prev_vmm_ctx = curr->proc->vmm_context;
    }

    if (!ARCH_CONTEXT_ISUSER(&next->ctx)) {
        com_mm_vmm_switch(NULL);
    } else if (NULL != next_vmm_ctx && next_vmm_ctx != prev_vmm_ctx) {
        com_mm_vmm_switch(next_vmm_ctx);
    }
}

// Caller holds thread authority and the waitlist lock. These helpers only
// change membership and callers supply the state transition (if any)
static void sched_waitlist_insert(com_waitlist_t *waitlist,
                                  com_thread_t   *thread,
                                  bool            first) {
    KASSERT(NULL == thread->waiting_on);
    thread->waiting_on = waitlist;
    if (first) {
        TAILQ_INSERT_HEAD(&waitlist->queue, thread, threads);
    } else {
        TAILQ_INSERT_TAIL(&waitlist->queue, thread, threads);
    }
    waitlist->count++;
}

static void sched_waitlist_remove(com_thread_t *thread) {
    com_waitlist_t *waitlist = thread->waiting_on;
    KASSERT(NULL != waitlist);
    TAILQ_REMOVE(&waitlist->queue, thread, threads);
    waitlist->count--;
    thread->waiting_on = NULL;
}

static void sched_handle_timeout(com_callout_t *callout) {
    com_thread_t *thread = callout->arg;
    com_sys_thread_lock(thread);
    if (callout->value == thread->epoch && NULL != thread->waiting_on) {
        thread->wait_error = ETIMEDOUT;
        com_sys_sched_notify_thread_nolock(thread);
    }
    com_sys_thread_unlock(thread);
    com_sys_thread_release(thread);
}

void com_sys_sched_switch_finish(arch_context_message_t message) {
    // Keep completion non-preemptible, then restore the incoming lock depth
    kspinlock_fake_acquire();
    kspinlock_release(&ARCH_CPU_GET()->runqueue_lock);
    if (NULL != message.callback) {
        message.callback(message.arg, message.value, true);
    }
    kspinlock_fake_release();
}

int com_sys_sched_yield_nolock(bool                   interruptible,
                               arch_context_message_t message) {
    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_SCHED_YIELD);
    arch_cpu_t   *cpu  = ARCH_CPU_GET();
    com_thread_t *curr = cpu->thread;
    KASSERT(NULL == curr || KSPINLOCK_IS_HELD(curr->sched_lock));
    KASSERT(NULL == curr || 1 == curr->lock_depth || 2 == curr->lock_depth);
    com_thread_t *next = TAILQ_FIRST(&cpu->sched_queue);

    if (NULL == curr) {
        com_sys_profiler_end_function(&profiler_data);
        return 0;
    }

    if (interruptible && COM_IPC_SIGNAL_NONE != com_ipc_signal_check()) {
        com_sys_profiler_end_function(&profiler_data);
        return EINTR;
    }

    if (NULL == next) {
        if (E_COM_THREAD_STATE_RUNNING ==
            __atomic_load_n(&curr->state, __ATOMIC_RELAXED)) {
            com_sys_profiler_end_function(&profiler_data);
            com_sys_sched_switch_finish(message);
            return 0;
        }

        next = cpu->idle_thread;
    } else {
        KASSERT(next != cpu->idle_thread);
        TAILQ_REMOVE(&cpu->sched_queue, next, threads);
    }

    if (curr == next) {
        KURGENT("curr = %p, next = %p | curr->tid = %d, next->tid = %d | "
                "IS_USER(curr) = %d, "
                "IS_USER(next) = %d",
                curr,
                next,
                curr->tid,
                next->tid,
                ARCH_CONTEXT_ISUSER(&curr->ctx),
                ARCH_CONTEXT_ISUSER(&next->ctx));
        KASSERT(curr != next);
    }

    if (E_COM_THREAD_STATE_RUNNING ==
        __atomic_load_n(&curr->state, __ATOMIC_RELAXED)) {
        com_sys_thread_transition_nolock(curr, E_COM_THREAD_STATE_READY);
        if (curr != cpu->idle_thread) {
            TAILQ_INSERT_TAIL(&cpu->sched_queue, curr, threads);
        }
    }
    KASSERT(E_COM_THREAD_STATE_RUNNING !=
            __atomic_load_n(&curr->state, __ATOMIC_RELAXED));

    curr->cpu      = NULL;
    curr->last_cpu = cpu;

    sched_switch_vmm_context(curr, next);

    ARCH_CONTEXT_SAVE_EXTRA(curr->xctx);
    if (ARCH_CONTEXT_ISUSER(&next->ctx)) {
        ARCH_CONTEXT_RESTORE_EXTRA(next->xctx);
    }

    com_sys_profiler_end_function(&profiler_data);

    cpu->thread = next;
    next->cpu   = cpu;
    com_sys_thread_transition_nolock(next, E_COM_THREAD_STATE_RUNNING);

    // Restore kernel stack
    ARCH_CPU_SET_INTERRUPT_STACK(cpu, next->kernel_stack);

    // Switch threads
    // NOTE: this doesn't jump to userspace directly! Instead, this switches the
    //        stack pointer and other registers so that when returning from the
    //        underlying assembly function, control is handed to whatever
    //        address is on the stack, which is most likely this function. Once
    //        the execution gets back here, com_sys_sched_yield can return to
    //        its caller, which can be a hardware timer or anything else (e.g.,
    //        wait). Remember that, since the stack has changed, there's no
    //        guarantee that this function will return to the same point from
    //        which it was called, in fact, quite the opposite.
    KASSERT(next->sched_lock == &cpu->runqueue_lock);
#if CONFIG_TRACK_SLEEP_TIME
    uintmax_t sleep_start = ARCH_CPU_GET_TIMESTAMP();
#endif
    arch_context_message_t incoming = ARCH_CONTEXT_SWITCH(&next->ctx,
                                                          &curr->ctx,
                                                          message.callback,
                                                          message.arg,
                                                          message.value);
#if CONFIG_TRACK_SLEEP_TIME
    // NOTE: this goes here, otherwise it may sample after interrupts are turned
    // back on and the value may be wrong due to nested preemption
    uintmax_t sleep_end = ARCH_CPU_GET_TIMESTAMP();
#endif
    com_sys_sched_switch_finish(incoming);
#if CONFIG_TRACK_SLEEP_TIME
    uintmax_t time_slept = sleep_end - sleep_start;
    __atomic_add_fetch(&curr->time_slept, time_slept, __ATOMIC_RELAXED);
#endif
    return 0;
}

void com_sys_sched_yield(void) {
    com_sys_thread_reap();
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL != curr_thread) {
        com_sys_thread_lock(curr_thread);
    }
    com_sys_sched_yield_nolock(false, (arch_context_message_t){0});
    // lock released elsewhere
}

void com_sys_sched_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
    (void)ctx;
    com_sys_sched_yield();
}

int com_sys_sched_wait(com_waitlist_t             *waitlist,
                       uintmax_t                   timeout,
                       bool                        interruptible,
                       const com_sched_wait_ops_t *ops,
                       void                       *primitive) {
    com_thread_t *curr = ARCH_CPU_GET_THREAD();
    KASSERT(curr != ARCH_CPU_GET()->idle_thread);

    if (NULL != ops && NULL != ops->prepare) {
        ops->prepare(primitive);
    }

    com_sys_thread_lock(curr);
    curr->wait_interruptible = interruptible;
    curr->wait_error         = 0;

    kspinlock_acquire(&waitlist->lock);
    sched_waitlist_insert(waitlist, curr, false);
    com_sys_thread_transition_nolock(curr, E_COM_THREAD_STATE_WAITING);
    kspinlock_release(&waitlist->lock);

    if (timeout) {
        // The firing owns this reference: a detached timeout may outlive
        // this wait and even the thread's eventual exit
        com_sys_thread_hold(curr);
        curr->timed_wait_callout->value = curr->epoch;
        com_sys_callout_set_and_enqueue(curr->timed_wait_callout,
                                        sched_handle_timeout,
                                        timeout);
    }

    arch_context_message_t message = {0};
    if (NULL != ops) {
        message = ops->release(primitive);
    }

    int ret = com_sys_sched_yield_nolock(interruptible, message);
    if (0 != ret) {
        // The switch did not consume the message or release authority
        kspinlock_acquire(&waitlist->lock);
        sched_waitlist_remove(curr);
        com_sys_thread_transition_nolock(curr, E_COM_THREAD_STATE_RUNNING);
        kspinlock_release(&waitlist->lock);
        com_sys_thread_unlock(curr);
        if (NULL != message.callback) {
            message.callback(message.arg, message.value, false);
        }
    }

    if (timeout && com_sys_callout_cancel(curr->timed_wait_callout)) {
        com_sys_thread_release(curr);
    }

    if (0 == ret) {
        ret = curr->wait_error;
    }

    com_sys_thread_reap();

    if (NULL != ops) {
        ops->acquire(primitive);
    }

    return ret;
}

void com_sys_sched_wait_nodrop(com_waitlist_t *waitlist) {
    com_sys_sched_wait(waitlist, 0, true, NULL, NULL);
}

int com_sys_sched_wait_nodrop_timeout(com_waitlist_t *waitlist,
                                      uintmax_t       timeout) {
    return com_sys_sched_wait(waitlist, timeout, true, NULL, NULL);
}

void com_sys_sched_notify_n(com_waitlist_t *waitlist, size_t num_waiters) {
    while (num_waiters > 0) {
        kspinlock_acquire(&waitlist->lock);
        com_thread_t *thread = TAILQ_FIRST(&waitlist->queue);
        if (NULL == thread) {
            kspinlock_release(&waitlist->lock);
            return;
        }
        // Only retain the immortal lock pointer across the unlock. Neither
        // the thread nor the sampled head needs to survive this slow path
        kspinlock_t *lock = __atomic_load_n(&thread->sched_lock,
                                            __ATOMIC_ACQUIRE);
        kspinlock_release(&waitlist->lock);
        kspinlock_acquire(lock);
        kspinlock_acquire(&waitlist->lock);
        thread = TAILQ_FIRST(&waitlist->queue);
        if (NULL != thread &&
            lock == __atomic_load_n(&thread->sched_lock, __ATOMIC_ACQUIRE)) {
            sched_waitlist_remove(thread);
            com_sys_thread_ready_nolock(thread);
            num_waiters--;
        }
        kspinlock_release(&waitlist->lock);
        kspinlock_release(lock);
    }
}

void com_sys_sched_notify(com_waitlist_t *waitlist) {
    com_sys_sched_notify_n(waitlist, 1);
}

void com_sys_sched_notify_all(com_waitlist_t *waitlist) {
    kspinlock_acquire(&waitlist->lock);
    size_t count = waitlist->count;
    kspinlock_release(&waitlist->lock);
    com_sys_sched_notify_n(waitlist, count);
}

void com_sys_sched_notify_thread_nolock(com_thread_t *thread) {
    com_waitlist_t *waitlist = thread->waiting_on;
    if (NULL != waitlist) {
        kspinlock_acquire(&waitlist->lock);
        sched_waitlist_remove(thread);
        com_sys_thread_ready_nolock(thread);
        kspinlock_release(&waitlist->lock);
    }
}

// All asynchronous interruption, including termination, obeys wait policy
void com_sys_sched_interrupt_nolock(com_thread_t *thread) {
    com_thread_state_t state = __atomic_load_n(&thread->state,
                                               __ATOMIC_RELAXED);
    if (E_COM_THREAD_STATE_WAITING == state && thread->wait_interruptible) {
        thread->wait_error = EINTR;
        com_sys_sched_notify_thread_nolock(thread);
    } else if (E_COM_THREAD_STATE_READY == state) {
        com_sys_sched_prioritize_nolock(thread);
    } else if (E_COM_THREAD_STATE_RUNNING == state &&
               thread->cpu != ARCH_CPU_GET()) {
        ARCH_CPU_SEND_IPI(thread->cpu, ARCH_CPU_IPI_SIGNAL);
    }
}

void com_sys_sched_notify_thread(com_thread_t *thread) {
    com_sys_thread_lock(thread);
    com_sys_sched_notify_thread_nolock(thread);
    com_sys_thread_unlock(thread);
}

void com_sys_sched_prioritize(com_thread_t *thread) {
    com_sys_thread_lock(thread);
    com_sys_sched_prioritize_nolock(thread);
    com_sys_thread_unlock(thread);
}

void com_sys_sched_prioritize_nolock(com_thread_t *thread) {
    com_thread_state_t state = __atomic_load_n(&thread->state,
                                               __ATOMIC_RELAXED);
    if (E_COM_THREAD_STATE_WAITING == state) {
        com_waitlist_t *waitlist = thread->waiting_on;
        kspinlock_acquire(&waitlist->lock);
        sched_waitlist_remove(thread);
        sched_waitlist_insert(waitlist, thread, true);
        kspinlock_release(&waitlist->lock);
    } else if (E_COM_THREAD_STATE_READY == state) {
        arch_cpu_t *cpu = thread->last_cpu;
        if (thread != cpu->idle_thread) {
            TAILQ_REMOVE(&cpu->sched_queue, thread, threads);
            TAILQ_INSERT_HEAD(&cpu->sched_queue, thread, threads);
        }
    }
    // Reordering does not end a wait: keep its epoch so pending timeout and
    // deferred-notification messages remain valid
}

void com_sys_sched_init(void) {
    arch_cpu_t *curr_cpu = ARCH_CPU_GET();
    SLIST_INIT(&curr_cpu->zombies);
    curr_cpu->idle_thread = com_sys_thread_new_kernel(NULL, sched_idle, NULL);
    curr_cpu->idle_thread->cpu = curr_cpu;
    // NOTE: this is different from other CPUs because those run the idle thread
    // as their first thread, while the BSP runs the init thread as its first
    // thread
    com_sys_thread_transition(curr_cpu->idle_thread, E_COM_THREAD_STATE_READY);
}
