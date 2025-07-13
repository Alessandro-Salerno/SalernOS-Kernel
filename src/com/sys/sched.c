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
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/mmu.h>
#include <lib/hashmap.h>
#include <lib/util.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

static khashmap_t              ZombieProcMap;
static com_spinlock_t          ZombieProcLock = COM_SPINLOCK_NEW();
static struct com_thread_tailq ZombieThreadQueue;

static void sched_idle(void) {
    ARCH_CPU_GET_THREAD()->lock_depth = 0;
    for (;;) {
        ARCH_CPU_ENABLE_INTERRUPTS();
        ARCH_CPU_HALT();
    }
}

static inline void *read_cr3(void) {
    void *value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static void sched_reaper_thread(void) {
    while (true) {
        com_spinlock_acquire(&ZombieProcLock);
        for (com_thread_t *zombie;
             NULL != (zombie = TAILQ_FIRST(&ZombieThreadQueue));) {
            com_spinlock_acquire(&zombie->sched_lock);
            ARCH_CPU_GET_THREAD()->lock_depth--;
            com_proc_t *proc = zombie->proc;
            TAILQ_REMOVE_HEAD(&ZombieThreadQueue, threads);
            com_sys_thread_destroy(zombie);
            if (proc->exited) {
                KHASHMAP_PUT(&ZombieProcMap, &proc->pid, proc);
            }
        }
        com_spinlock_release(&ZombieProcLock);
        KHASHMAP_FOREACH(&ZombieProcMap) {
            com_proc_t *proc = entry->value;

            if (0 == __atomic_load_n(&proc->num_ref, __ATOMIC_SEQ_CST)) {
                KHASHMAP_REMOVE(&ZombieProcMap, &proc->pid);
                KASSERT(read_cr3() != proc->page_table);
                com_sys_proc_destroy(proc);
            }
        }
        com_sys_sched_yield();
    }
}

void com_sys_sched_yield_nolock(void) {
    arch_cpu_t *cpu = ARCH_CPU_GET();
    com_spinlock_acquire(&cpu->runqueue_lock);
    com_thread_t *curr = cpu->thread;
    KASSERT(curr->sched_lock);
    com_thread_t *next = TAILQ_FIRST(&cpu->sched_queue);

    if (NULL == curr) {
        com_spinlock_release(&cpu->runqueue_lock);
        return;
    }

    // NOTE: this seems like it would create a race condition, but it does not
    // because the reaper thread first acquires the sched_lock, which we are
    // holding here
    bool zombie_acquired = false;
    if (curr->exited) {
        com_spinlock_acquire(&ZombieProcLock);
        TAILQ_INSERT_TAIL(&ZombieThreadQueue, curr, threads);
        zombie_acquired = true;
    }
    while (NULL != next &&
           (next->exited || (NULL != next->proc && next->proc->exited))) {
        if (!zombie_acquired) {
            com_spinlock_acquire(&ZombieProcLock);
            zombie_acquired = true;
        }
        TAILQ_REMOVE_HEAD(&cpu->sched_queue, threads);
        TAILQ_INSERT_TAIL(&ZombieThreadQueue, next, threads);
        next = TAILQ_FIRST(&cpu->sched_queue);
    }
    if (zombie_acquired) {
        com_spinlock_release(&ZombieProcLock);
    }

    if (NULL != curr->proc) {
        KASSERT(read_cr3() == curr->proc->page_table);
    }

    if (NULL == next) {
        if (curr->runnable) {
            com_spinlock_release(&cpu->runqueue_lock);
            com_spinlock_release(&curr->sched_lock);
            return;
        }

        next = cpu->idle_thread;
    } else {
        TAILQ_REMOVE_HEAD(&cpu->sched_queue, threads);
    }

    KASSERT(curr != next);
    KASSERT(!curr->exited || !curr->runnable);

    if (curr->runnable && curr != cpu->idle_thread) {
        TAILQ_INSERT_TAIL(&cpu->sched_queue, curr, threads);
    }

    com_spinlock_release(&cpu->runqueue_lock);

    arch_mmu_pagetable_t *prev_pt = NULL;
    arch_mmu_pagetable_t *next_pt = NULL;

    if (NULL != curr->proc) {
        prev_pt = curr->proc->page_table;
    }

    if (NULL != next->proc) {
        next_pt = next->proc->page_table;
    }

    if (!ARCH_CONTEXT_ISUSER(&next->ctx)) {
        arch_mmu_switch_default();
    } else if (NULL != next_pt && next_pt != prev_pt) {
        arch_mmu_switch(next_pt);
    }

    ARCH_CONTEXT_SAVE_EXTRA(curr->xctx);
    if (ARCH_CONTEXT_ISUSER(&next->ctx)) {
        ARCH_CONTEXT_RESTORE_EXTRA(next->xctx);
    }

    if (NULL != next->proc) {
        KASSERT(read_cr3() == next->proc->page_table);
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
    com_spinlock_acquire(&ARCH_CPU_GET_THREAD()->sched_lock);
    com_sys_sched_yield_nolock();
    // lock released elsewhere
}

void com_sys_sched_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
    (void)ctx;
    com_sys_sched_yield();
}

void com_sys_sched_wait(struct com_thread_tailq *waiting_on,
                        com_spinlock_t          *cond) {
    com_thread_t *curr = ARCH_CPU_GET_THREAD();
    com_spinlock_acquire(&curr->sched_lock);
    com_spinlock_acquire(&ARCH_CPU_GET()->runqueue_lock);
    if (NULL != curr->cpu) {
        curr->cpu = NULL;
    }
    curr->waiting_on   = waiting_on;
    curr->waiting_cond = cond;
    curr->runnable     = false;
    TAILQ_INSERT_TAIL(waiting_on, curr, threads);
    com_spinlock_release(&ARCH_CPU_GET()->runqueue_lock);

    com_spinlock_release(cond);
    com_sys_sched_yield_nolock();

    // This point is reached AFTER the thread is notified
    com_spinlock_acquire(cond);
}

void com_sys_sched_notify(struct com_thread_tailq *waiters) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    arch_cpu_t   *currcpu     = ARCH_CPU_GET();
    com_thread_t *next        = TAILQ_FIRST(waiters);
    com_spinlock_acquire(&curr_thread->sched_lock);

    if (NULL != next) {
        com_spinlock_acquire(&next->sched_lock);
        KASSERT(NULL == next->cpu);
        TAILQ_REMOVE_HEAD(waiters, threads);
        com_spinlock_acquire(&currcpu->runqueue_lock);
        TAILQ_INSERT_HEAD(&currcpu->sched_queue, next, threads);
        next->runnable     = true;
        next->waiting_on   = NULL;
        next->waiting_cond = NULL;
        com_spinlock_release(&currcpu->runqueue_lock);
        com_spinlock_release(&next->sched_lock);
    }

    com_spinlock_release(&curr_thread->sched_lock);
}

void com_sys_sched_notify_all(struct com_thread_tailq *waiters) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    arch_cpu_t   *currcpu     = ARCH_CPU_GET();
    com_spinlock_acquire(&curr_thread->sched_lock);

    for (com_thread_t *next; NULL != (next = TAILQ_FIRST(waiters));) {
        com_spinlock_acquire(&next->sched_lock);
        KASSERT(NULL == next->cpu);
        TAILQ_REMOVE_HEAD(waiters, threads);
        com_spinlock_acquire(&currcpu->runqueue_lock);
        next->cpu = currcpu;
        TAILQ_INSERT_HEAD(&currcpu->sched_queue, next, threads);
        next->runnable     = true;
        next->waiting_on   = NULL;
        next->waiting_cond = NULL;
        com_spinlock_release(&currcpu->runqueue_lock);
        com_spinlock_release(&next->sched_lock);
    }

    com_spinlock_release(&curr_thread->sched_lock);
}

void com_sys_sched_init(void) {
    arch_cpu_t *curr_cpu  = ARCH_CPU_GET();
    curr_cpu->idle_thread = com_sys_thread_new_kernel(NULL, sched_idle);
}

void com_sys_sched_init_base(void) {
    KLOG("initializing scheduler (first)");
    KHASHMAP_INIT(&ZombieProcMap);
    TAILQ_INIT(&ZombieThreadQueue);
    com_thread_t *reaper_thread =
        com_sys_thread_new_kernel(NULL, sched_reaper_thread);
    reaper_thread->runnable = true;
    TAILQ_INSERT_TAIL(&ARCH_CPU_GET()->sched_queue, reaper_thread, threads);
}
