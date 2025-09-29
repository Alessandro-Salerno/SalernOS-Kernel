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

static void sched_idle(void) {
    ARCH_CPU_GET_THREAD()->lock_depth = 0;
    for (;;) {
        ARCH_CPU_ENABLE_INTERRUPTS();
        ARCH_CPU_HALT();
    }
}

static void sched_reaper_thread(void) {
    while (true) {
        kspinlock_acquire(&ZombieProcLock);
        for (com_thread_t *zombie;
             NULL != (zombie = TAILQ_FIRST(&ZombieThreadQueue));) {
            kspinlock_acquire(&zombie->sched_lock);
            ARCH_CPU_GET_THREAD()->lock_depth--;
            com_proc_t *proc = zombie->proc;
            TAILQ_REMOVE_HEAD(&ZombieThreadQueue, threads);
            com_sys_thread_destroy(zombie);
            if (proc->exited) {
                KHASHMAP_PUT(&ZombieProcMap, &proc->pid, proc);
            }
        }
        kspinlock_release(&ZombieProcLock);
        KHASHMAP_FOREACH(&ZombieProcMap) {
            com_proc_t *proc = entry->value;

            if (0 == __atomic_load_n(&proc->num_ref, __ATOMIC_SEQ_CST)) {
                KHASHMAP_REMOVE(&ZombieProcMap, &proc->pid);
                com_sys_proc_destroy(proc);
            }
        }
        com_sys_sched_yield();
    }
}

void com_sys_sched_yield_nolock(void) {
    arch_cpu_t *cpu = ARCH_CPU_GET();
    kspinlock_acquire(&cpu->runqueue_lock);
    com_thread_t *curr = cpu->thread;
    KASSERT(NULL == curr || curr->sched_lock);
    com_thread_t *next = TAILQ_FIRST(&cpu->sched_queue);

    if (NULL == curr) {
        kspinlock_release(&cpu->runqueue_lock);
        return;
    }

    // NOTE: this seems like it would create a race condition, but it does not
    // because the reaper thread first acquires the sched_lock, which we are
    // holding here
    bool zombie_acquired = false;
    if (curr->exited) {
        kspinlock_acquire(&ZombieProcLock);
        TAILQ_INSERT_TAIL(&ZombieThreadQueue, curr, threads);
        zombie_acquired = true;
    }
    while (NULL != next &&
           (next->exited || (NULL != next->proc && next->proc->exited))) {
        if (!zombie_acquired) {
            kspinlock_acquire(&ZombieProcLock);
            zombie_acquired = true;
        }
        TAILQ_REMOVE_HEAD(&cpu->sched_queue, threads);
        TAILQ_INSERT_TAIL(&ZombieThreadQueue, next, threads);
        next = TAILQ_FIRST(&cpu->sched_queue);
    }
    if (zombie_acquired) {
        kspinlock_release(&ZombieProcLock);
    }

    if (NULL == next) {
        if (curr->runnable) {
            kspinlock_release(&cpu->runqueue_lock);
            kspinlock_release(&curr->sched_lock);
            return;
        }

        next = cpu->idle_thread;
    } else {
        KASSERT(next != cpu->idle_thread);
        TAILQ_REMOVE_HEAD(&cpu->sched_queue, threads);
    }

    KASSERT(curr != next);
    KASSERT(!curr->exited || !curr->runnable);

    if (curr->runnable && curr != cpu->idle_thread) {
        TAILQ_INSERT_TAIL(&cpu->sched_queue, curr, threads);
    }

    kspinlock_release(&cpu->runqueue_lock);

    com_vmm_context_t *prev_vmm_ctx = NULL;
    com_vmm_context_t *next_vmm_ctx = NULL;

    if (NULL != curr->proc) {
        prev_vmm_ctx = curr->proc->vmm_context;
    }

    if (NULL != next->proc) {
        next_vmm_ctx = next->proc->vmm_context;
    }

    if (!ARCH_CONTEXT_ISUSER(&next->ctx)) {
        com_mm_vmm_switch(NULL);
    } else if (NULL != next_vmm_ctx && next_vmm_ctx != prev_vmm_ctx) {
        com_mm_vmm_switch(next_vmm_ctx);
    }

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
    ARCH_CONTEXT_SWITCH(&next->ctx, &curr->ctx, &curr->sched_lock);
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
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    arch_cpu_t   *currcpu     = ARCH_CPU_GET();
    kspinlock_acquire(&curr_thread->sched_lock);

    for (com_thread_t *next; NULL != (next = TAILQ_FIRST(waiters));) {
        kspinlock_acquire(&next->sched_lock);
        KASSERT(NULL == next->cpu);
        TAILQ_REMOVE_HEAD(waiters, threads);
        kspinlock_acquire(&currcpu->runqueue_lock);
        next->cpu = currcpu;
        TAILQ_INSERT_HEAD(&currcpu->sched_queue, next, threads);
        next->runnable     = true;
        next->waiting_on   = NULL;
        next->waiting_cond = NULL;
        kspinlock_release(&currcpu->runqueue_lock);
        kspinlock_release(&next->sched_lock);
    }

    kspinlock_release(&curr_thread->sched_lock);
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
    KLOG("initializing scheduler (first)");
    KHASHMAP_INIT(&ZombieProcMap);
    TAILQ_INIT(&ZombieThreadQueue);
    com_thread_t *reaper_thread = com_sys_thread_new_kernel(
        NULL,
        sched_reaper_thread);
    reaper_thread->runnable = true;
    TAILQ_INSERT_TAIL(&ARCH_CPU_GET()->sched_queue, reaper_thread, threads);
}
