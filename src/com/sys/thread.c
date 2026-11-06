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
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/context.h>
#include <kernel/platform/x86-64/smp.h>
#include <lib/spinlock.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/uio.h>
#include <vendor/tailq.h>

_Static_assert(CONFIG_IOV_MAX * sizeof(struct iovec) <= ARCH_PAGE_SIZE,
               "the scratch page must hold CONFIG_IOV_MAX iovecs");

static pid_t NextTid = 0;

// CPU locks outlive every thread. Recheck after acquiring a possibly stale
// lock
void com_sys_thread_lock(com_thread_t *thread) {
    for (;;) {
        kspinlock_t *lock = __atomic_load_n(&thread->sched_lock,
                                            __ATOMIC_ACQUIRE);
        kspinlock_acquire(lock);
        if (lock == __atomic_load_n(&thread->sched_lock, __ATOMIC_ACQUIRE)) {
            return;
        }
        kspinlock_release(lock);
    }
}

void com_sys_thread_unlock(com_thread_t *thread) {
    kspinlock_release(__atomic_load_n(&thread->sched_lock, __ATOMIC_RELAXED));
}

void com_sys_thread_hold(com_thread_t *thread) {
    __atomic_add_fetch(&thread->refs, 1, __ATOMIC_RELAXED);
}

void com_sys_thread_release(com_thread_t *thread) {
    if (0 == __atomic_sub_fetch(&thread->refs, 1, __ATOMIC_ACQ_REL)) {
        KASSERT(E_COM_THREAD_STATE_EXITED ==
                __atomic_load_n(&thread->state, __ATOMIC_RELAXED));
        KASSERT(NULL == thread->cpu && NULL == thread->waiting_on);
        // Reclamation can sleep: never free from a switch-in or idle context
        kspinlock_fake_acquire();
        SLIST_INSERT_HEAD(&ARCH_CPU_GET()->zombies, thread, zombies);
        kspinlock_fake_release();
    }
}

void com_sys_thread_reap(void) {
    com_thread_t *curr = ARCH_CPU_GET_THREAD();
    if (NULL == curr || curr == ARCH_CPU_GET()->idle_thread ||
        curr->lock_depth) {
        return;
    }
    kspinlock_fake_acquire();
    struct com_thread_slist zombies = ARCH_CPU_GET()->zombies;
    SLIST_INIT(&ARCH_CPU_GET()->zombies);
    kspinlock_fake_release();
    while (!SLIST_EMPTY(&zombies)) {
        com_thread_t *list = SLIST_FIRST(&zombies);
        SLIST_REMOVE_HEAD(&zombies, zombies);
        if (NULL != list->proc) {
            COM_SYS_PROC_RELEASE(list->proc);
        }
        com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(list->kernel_stack) -
                        ARCH_PAGE_SIZE);
        com_sys_callout_destroy(list->timed_wait_callout);
        com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(list->scratch));
        com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(list));
    }
}

static com_thread_t *new_thread(com_proc_t *proc, arch_context_t ctx) {
    com_thread_t *thread = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
    thread->proc         = proc;
    thread->ctx          = ctx;
    thread->lock_depth   = 1;
    thread->last_cpu     = ARCH_CPU_GET();
    __atomic_store_n(&thread->sched_lock,
                     &thread->last_cpu->runqueue_lock,
                     __ATOMIC_RELEASE);
    // Scheduler ownership lasts until thread_exit_complete, after switch-out
    thread->refs               = 1;
    thread->epoch              = 0;
    thread->wait_interruptible = false;
    thread->exit_requested     = false;
    thread->signal_stopped     = false;
    thread->cpu                = NULL;
    __atomic_store_n(&thread->state, E_COM_THREAD_STATE_NEW, __ATOMIC_RELAXED);
    thread->kernel_stack = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc()) +
                           ARCH_PAGE_SIZE;
    ARCH_CONTEXT_INIT_EXTRA(thread->xctx);
    thread->tid = __atomic_fetch_add(&NextTid, 1, __ATOMIC_RELAXED);

    thread->usercopy_ctx       = NULL;
    thread->scratch            = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
    thread->waiting_on         = NULL;
    thread->ctime              = ARCH_CPU_GET_TIMESTAMP();
    thread->timed_wait_callout = com_sys_callout_new(NULL, thread, 0);
    thread->timed_wait_callout->permanent = true;
    thread->timed_wait_callout->snapshot  = true;

    thread->real_timer.lock = KSPINLOCK_NEW();

    COM_IPC_SIGNAL_SIGMASK_INIT(&thread->pending_signals);
    COM_IPC_SIGNAL_SIGMASK_INIT(&thread->masked_signals);

    if (NULL != proc) {
        com_sys_proc_add_thread(proc, thread);
    }

    return thread;
}

com_thread_t *com_sys_thread_new(com_proc_t *proc,
                                 void       *stack,
                                 uintmax_t   stack_size,
                                 void       *entry) {
    arch_context_t ctx = {0};
    ARCH_CONTEXT_THREAD_SET(ctx, stack, stack_size, entry);
    return new_thread(proc, ctx);
}

com_thread_t *
com_sys_thread_new_kernel(com_proc_t *proc, void *entry, void *arg) {
    arch_context_t ctx = {0};
    com_thread_t  *t   = new_thread(proc, ctx);
    ARCH_CONTEXT_THREAD_SET_KERNEL(t->ctx, t->kernel_stack, entry, arg);
    return t;
}

void com_sys_thread_transition(com_thread_t *thread, com_thread_state_t state) {
    com_sys_thread_lock(thread);
    com_sys_thread_transition_nolock(thread, state);
    com_sys_thread_unlock(thread);
}

void com_sys_thread_transition_nolock(com_thread_t      *thread,
                                      com_thread_state_t state) {
    // debug checks
    KASSERT(E_COM_THREAD_STATE_EXITED !=
            __atomic_load_n(&thread->state, __ATOMIC_RELAXED));
    switch (state) {
        case E_COM_THREAD_STATE_NEW:
            KASSERT(!"cannot transition to new state from any state");
            break;
        case E_COM_THREAD_STATE_READY:
            KASSERT(E_COM_THREAD_STATE_EXITED !=
                    __atomic_load_n(&thread->state, __ATOMIC_RELAXED));
            KASSERT(NULL == thread->waiting_on);
            break;
        case E_COM_THREAD_STATE_WAITING:
            KASSERT(NULL != thread->waiting_on);
            break;
        case E_COM_THREAD_STATE_RUNNING:
            KASSERT(NULL != thread->cpu);
            KASSERT(NULL == thread->waiting_on);
            break;
        case E_COM_THREAD_STATE_EXITED:
            break;
        default:
            KASSERT(!"invalid argument");
            break;
    }

    __atomic_store_n(&thread->state, state, __ATOMIC_RELAXED);
    __atomic_add_fetch(&thread->epoch, 1, __ATOMIC_RELAXED);
}

void com_sys_thread_exit(com_thread_t *thread) {
    com_sys_thread_lock(thread);
    com_sys_thread_exit_nolock(thread);
}

void com_sys_thread_request_exit(com_thread_t *thread) {
    com_sys_thread_lock(thread);
    __atomic_store_n(&thread->exit_requested, true, __ATOMIC_RELEASE);
    com_sys_sched_interrupt_nolock(thread);
    com_sys_thread_unlock(thread);
}

static void thread_exit_complete(void *arg, uintptr_t value, bool success) {
    (void)value;
    KASSERT(success);
    com_thread_t *thread = arg;
    if (NULL != thread->proc) {
        // Acknowledge only after switching away, including the VMM context
        com_sys_proc_remove_thread(thread->proc, thread);
    }
    com_sys_thread_release(thread);
}

void com_sys_thread_exit_nolock(com_thread_t *thread) {
    KASSERT(thread == ARCH_CPU_GET_THREAD());
    KASSERT(E_COM_THREAD_STATE_RUNNING ==
            __atomic_load_n(&thread->state, __ATOMIC_RELAXED));
    __atomic_store_n(&thread->exit_requested, true, __ATOMIC_RELEASE);
    com_sys_thread_transition_nolock(thread, E_COM_THREAD_STATE_EXITED);
    com_sys_sched_yield_nolock(
        false,
        (arch_context_message_t){.callback = thread_exit_complete,
                                 .arg      = thread});
    __builtin_unreachable();
}

void com_sys_thread_destroy(com_thread_t *thread) {
    KASSERT(E_COM_THREAD_STATE_EXITED ==
            __atomic_load_n(&thread->state, __ATOMIC_RELAXED));
    KASSERT(NULL == thread->cpu && NULL == thread->waiting_on);
    com_sys_thread_release(thread);
}

void com_sys_thread_ready_nolock(com_thread_t *thread) {
    arch_cpu_t *ready_cpu = thread->last_cpu;
    KASSERT(NULL == thread->waiting_on);
    KASSERT(thread->sched_lock == &ready_cpu->runqueue_lock);
    com_sys_thread_transition_nolock(thread, E_COM_THREAD_STATE_READY);
    TAILQ_INSERT_TAIL(&ready_cpu->sched_queue, thread, threads);

    KDEBUG("thread with tid=%zu is now runnable on cpu %zu",
           thread->tid,
           ready_cpu->id);

    ARCH_CPU_SEND_IPI(ready_cpu, ARCH_CPU_IPI_RESCHEDULE);
}

void com_sys_thread_ready(com_thread_t *thread) {
    arch_cpu_t *old  = thread->last_cpu;
    arch_cpu_t *next = x86_64_smp_get_random();
    // This entry point publishes a NEW thread, still owned by its creator
    KASSERT(E_COM_THREAD_STATE_NEW ==
            __atomic_load_n(&thread->state, __ATOMIC_RELAXED));
    arch_cpu_t *first  = old->id < next->id ? old : next;
    arch_cpu_t *second = old->id < next->id ? next : old;
    kspinlock_acquire(&first->runqueue_lock);
    if (first != second) {
        kspinlock_acquire(&second->runqueue_lock);
    }
    thread->last_cpu = next;
    __atomic_store_n(&thread->sched_lock,
                     &next->runqueue_lock,
                     __ATOMIC_RELEASE);
    com_sys_thread_ready_nolock(thread);
    if (first != second) {
        kspinlock_release(&second->runqueue_lock);
    }
    kspinlock_release(&first->runqueue_lock);
}
