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
#include <arch/cpu.h>
#include <arch/info.h>
#include <arch/mmu.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/mmu.h>
#include <lib/hashmap.h>
#include <lib/spinlock.h>
#include <lib/util.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

static khashmap_t              ZombieProcMap;
static kspinlock_t             ZombieProcLock = KSPINLOCK_NEW();
static struct com_thread_tailq ZombieThreadQueue;
static struct com_thread_tailq ReaperThreadWaitlist;
static size_t                  ReaperNumPending = 0;

static void sched_reap_zombies(struct com_thread_tailq *local_zombies) {
    size_t num_done_threads = 0;

    // We want to hold the lock as little as possible because while holding
    // it, there's no preemption. So we move threads from the global queue
    // (which is guarded by the lock) into a local queue that we can access
    // freely after releasing the lock
    for (com_thread_t *zombie;
         NULL != (zombie = TAILQ_FIRST(&ZombieThreadQueue)) &&
         num_done_threads < CONFIG_SCHED_REAPER_MAX_THREADS;
         num_done_threads++) {
        TAILQ_REMOVE_HEAD(&ZombieThreadQueue, threads);
        TAILQ_INSERT_TAIL(local_zombies, zombie, threads);
    }

    ReaperNumPending -= num_done_threads;
    kspinlock_release(&ZombieProcLock);

    com_thread_t *zombie, *_;
    TAILQ_FOREACH_SAFE(zombie, local_zombies, threads, _) {
        kspinlock_acquire(&zombie->sched_lock);
        kspinlock_fake_release();

        com_proc_t *proc = zombie->proc;
        TAILQ_REMOVE_HEAD(local_zombies, threads);
        KASSERT(zombie->exited);
        KASSERT(NULL != zombie);
        KASSERT(zombie != ARCH_CPU_GET_THREAD());
        KASSERT(ARCH_CONTEXT_ISUSER(&zombie->ctx));
        KASSERT(NULL != proc);
        com_sys_thread_destroy(zombie);
        if (proc->exited) {
            KHASHMAP_PUT(&ZombieProcMap, &proc->pid, proc);
        }
    }

    // This is not locked because we are the only ones accessing it
    size_t num_done_procs = 0;
    KHASHMAP_FOREACH(&ZombieProcMap) {
        if (CONFIG_SCHED_REAPER_MAX_PROCS == num_done_procs) {
            break;
        }

        com_proc_t *proc = entry->value;
        if (0 == __atomic_load_n(&proc->num_ref, __ATOMIC_SEQ_CST)) {
            KHASHMAP_REMOVE(&ZombieProcMap, &proc->pid);
            com_sys_proc_destroy(proc);
            num_done_procs++;
        }
    }

    KASSERT(TAILQ_EMPTY(local_zombies));
}

static void sched_reaper_thread(void) {
    bool                    already_done = false;
    struct com_thread_tailq local_zombies;
    TAILQ_INIT(&local_zombies);

    while (true) {
        // Allow kernel preemption
        com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
        curr_thread->lock_depth   = 0;

        kspinlock_acquire(&ZombieProcLock);

        // If there's no thread to kill, we just wait so we don't waste CPU time
        if (TAILQ_EMPTY(&ZombieThreadQueue) || already_done) {
            already_done = false;
            com_sys_sched_wait(&ReaperThreadWaitlist, &ZombieProcLock);
        }

        sched_reap_zombies(&local_zombies);
        already_done = true;
    }
}

static inline bool sched_notify_reaper(void) {
    arch_cpu_t   *curr_cpu = ARCH_CPU_GET();
    com_thread_t *reaper   = TAILQ_FIRST(&ReaperThreadWaitlist);

    if (NULL == reaper) {
        return false;
    }

    kspinlock_acquire(&reaper->sched_lock);
    TAILQ_REMOVE_HEAD(&ReaperThreadWaitlist, threads);
    // Add to tail for "low priority"
    TAILQ_INSERT_TAIL(&curr_cpu->sched_queue, reaper, threads);
    reaper->runnable     = true;
    reaper->waiting_on   = NULL;
    reaper->waiting_cond = NULL;
    kspinlock_release(&reaper->sched_lock);

    return true;
}

static void sched_idle(void) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();

    for (;;) {
        curr_thread->lock_depth = 0;
        ARCH_CPU_ENABLE_INTERRUPTS();
        ARCH_CPU_HALT();
    }
}

static inline com_thread_t *sched_handle_zombies(bool         *next_removed,
                                                 arch_cpu_t   *cpu,
                                                 com_thread_t *curr,
                                                 com_thread_t *next) {
    // NOTE: I have no idea why, but the reaper thread MUST hold the sched_lock
    bool zombie_acquired = false;

    if (curr->exited) {
        kspinlock_acquire(&ZombieProcLock);
        // No remove from cpu rr since curr is not on the runqueue
        TAILQ_INSERT_TAIL(&ZombieThreadQueue, curr, threads);
        zombie_acquired = true;
        ReaperNumPending++;
    }

    while (NULL != next && next->exited) {
        if (!zombie_acquired) {
            kspinlock_acquire(&ZombieProcLock);
            zombie_acquired = true;
            *next_removed   = true;
        }
        ReaperNumPending++;
        TAILQ_REMOVE_HEAD(&cpu->sched_queue, threads);
        TAILQ_INSERT_TAIL(&ZombieThreadQueue, next, threads);
        next = TAILQ_FIRST(&cpu->sched_queue);
    }

    if (zombie_acquired) {
        if (ReaperNumPending >= CONFIG_SCHED_REAPER_NOTIFY) {
            sched_notify_reaper();
        }
        kspinlock_release(&ZombieProcLock);
    }

    return next;
}

static inline void sched_switch_vmm_context(com_thread_t *curr,
                                            com_thread_t *next) {
    com_vmm_context_t *prev_vmm_ctx = NULL;
    com_vmm_context_t *next_vmm_ctx = NULL;

    if (NULL != curr->proc) {
        __atomic_add_fetch(&curr->proc->num_running_threads,
                           -1,
                           __ATOMIC_SEQ_CST);
        prev_vmm_ctx = curr->proc->vmm_context;
    }

    if (NULL != next->proc) {
        __atomic_add_fetch(&next->proc->num_running_threads,
                           1,
                           __ATOMIC_SEQ_CST);
        next_vmm_ctx = next->proc->vmm_context;
    }

    if (!ARCH_CONTEXT_ISUSER(&next->ctx)) {
        com_mm_vmm_switch(NULL);
    } else if (NULL != next_vmm_ctx && next_vmm_ctx != prev_vmm_ctx) {
        com_mm_vmm_switch(next_vmm_ctx);
    }
}

void com_sys_sched_yield_nolock(void) {
    arch_cpu_t *cpu = ARCH_CPU_GET();
    kspinlock_acquire(&cpu->runqueue_lock);
    com_thread_t *curr = cpu->thread;
    KASSERT(NULL == curr || KSPINLOCK_IS_HELD(&curr->sched_lock));
    com_thread_t *next = TAILQ_FIRST(&cpu->sched_queue);

    if (NULL == curr) {
        kspinlock_release(&cpu->runqueue_lock);
        return;
    }

    bool next_removed = false;
    next              = sched_handle_zombies(&next_removed, cpu, curr, next);

    if (NULL == next) {
        if (curr->runnable) {
            kspinlock_release(&cpu->runqueue_lock);
            kspinlock_release(&curr->sched_lock);
            return;
        }

        next = cpu->idle_thread;
    } else if (!next_removed) {
        KASSERT(next != cpu->idle_thread);
        TAILQ_REMOVE(&cpu->sched_queue, next, threads);
    }

    // TODO: fix this as soon as I figure out why it's happening
    if (curr == next) {
        KURGENT("curr = %p, next = %p | curr->tid = %d, next->5id = %d | "
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
    KASSERT(!curr->exited || !curr->runnable);

    if (curr->runnable && curr != cpu->idle_thread) {
        TAILQ_INSERT_TAIL(&cpu->sched_queue, curr, threads);
    }

    kspinlock_release(&cpu->runqueue_lock);

    sched_switch_vmm_context(curr, next);

    ARCH_CONTEXT_SAVE_EXTRA(curr->xctx);
    if (ARCH_CONTEXT_ISUSER(&next->ctx)) {
        ARCH_CONTEXT_RESTORE_EXTRA(next->xctx);
    }

    cpu->thread = next;
    curr->cpu   = NULL;
    next->cpu   = cpu;

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
    next->lock_depth--;
    // This must drop the lock on curr->sched_lock
    ARCH_CONTEXT_SWITCH(&next->ctx, &curr->ctx, &curr->sched_lock.lock);
}

void com_sys_sched_yield(void) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL != curr_thread) {
        kspinlock_acquire(&curr_thread->sched_lock);
    }
    com_sys_sched_yield_nolock();
    // lock released elsewhere
}

void com_sys_sched_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
    (void)ctx;
    com_sys_sched_yield();
}

void com_sys_sched_wait(struct com_thread_tailq *waiting_on,
                        kspinlock_t             *cond) {
    com_thread_t *curr = ARCH_CPU_GET_THREAD();
    KASSERT(1 == curr->lock_depth);
    kspinlock_acquire(&curr->sched_lock);

    // curr->cpu is nulled and curr is removed from runqueue in sched, no need
    // to do it here. In fact, this has caused issues, specifically with
    // testjoin 1000

    curr->waiting_on   = waiting_on;
    curr->waiting_cond = cond;
    curr->runnable     = false;
    TAILQ_INSERT_TAIL(waiting_on, curr, threads);

    kspinlock_release(cond);
    com_sys_sched_yield_nolock();

    // This point is reached AFTER the thread is notified
    kspinlock_acquire(cond);
}

void com_sys_sched_notify(struct com_thread_tailq *waiters) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    arch_cpu_t   *currcpu     = ARCH_CPU_GET();
    com_thread_t *next        = TAILQ_FIRST(waiters);

    if (NULL != next) {
        kspinlock_acquire(&curr_thread->sched_lock);
        kspinlock_acquire(&next->sched_lock);
        KASSERT(NULL == next->cpu);
        TAILQ_REMOVE_HEAD(waiters, threads);
        kspinlock_acquire(&currcpu->runqueue_lock);
        TAILQ_INSERT_HEAD(&currcpu->sched_queue, next, threads);
        next->runnable     = true;
        next->waiting_on   = NULL;
        next->waiting_cond = NULL;
        kspinlock_release(&currcpu->runqueue_lock);
        kspinlock_release(&next->sched_lock);
        kspinlock_release(&curr_thread->sched_lock);
    }
}

void com_sys_sched_notify_all(struct com_thread_tailq *waiters) {
    // com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    arch_cpu_t *currcpu = ARCH_CPU_GET();
    // kspinlock_acquire(&curr_thread->sched_lock);

    com_thread_t *next, *_;
    TAILQ_FOREACH_SAFE(next, waiters, threads, _) {
        kspinlock_acquire(&next->sched_lock);
        KASSERT(NULL == next->cpu);
        TAILQ_REMOVE_HEAD(waiters, threads);
        kspinlock_acquire(&currcpu->runqueue_lock);
        TAILQ_INSERT_HEAD(&currcpu->sched_queue, next, threads);
        next->runnable     = true;
        next->waiting_on   = NULL;
        next->waiting_cond = NULL;
        kspinlock_release(&currcpu->runqueue_lock);
        kspinlock_release(&next->sched_lock);
    }

    // kspinlock_release(&curr_thread->sched_lock);
}

void com_sys_sched_notify_thread_nolock(com_thread_t *thread) {
    arch_cpu_t *currcpu = ARCH_CPU_GET();
    if (NULL != thread->waiting_on) {
        KASSERT(NULL == thread->cpu);
        TAILQ_REMOVE(thread->waiting_on, thread, threads);
        kspinlock_acquire(&currcpu->runqueue_lock);
        TAILQ_INSERT_HEAD(&currcpu->sched_queue, thread, threads);
        thread->runnable     = true;
        thread->waiting_on   = NULL;
        thread->waiting_cond = NULL;
        kspinlock_release(&currcpu->runqueue_lock);
    }
}

void com_sys_sched_notify_thread(com_thread_t *thread) {
    kspinlock_acquire(&thread->sched_lock);
    com_sys_sched_notify_thread_nolock(thread);
    kspinlock_release(&thread->sched_lock);
}

void com_sys_sched_init(void) {
    arch_cpu_t *curr_cpu  = ARCH_CPU_GET();
    curr_cpu->idle_thread = com_sys_thread_new_kernel(NULL, sched_idle);
}

void com_sys_sched_init_base(void) {
    KLOG("initializing scheduler reaper");
    KHASHMAP_INIT(&ZombieProcMap);
    TAILQ_INIT(&ZombieThreadQueue);
    TAILQ_INIT(&ReaperThreadWaitlist);

    com_thread_t *reaper_thread = com_sys_thread_new_kernel(
        NULL,
        sched_reaper_thread);
    reaper_thread->runnable = true;
    TAILQ_INSERT_TAIL(&ARCH_CPU_GET()->sched_queue, reaper_thread, threads);
}
