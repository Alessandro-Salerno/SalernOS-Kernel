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
#include <kernel/com/sys/thread.h>
#include <kernel/platform/mmu.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

static void sched_idle(void) {
    hdr_arch_cpu_get_thread()->lock_depth = 0;
    for (;;) {
        hdr_arch_cpu_interrupt_enable();
        ARCH_CPU_HALT();
    }
}

void com_sys_sched_yield_nolock(void) {
    arch_cpu_t *cpu = hdr_arch_cpu_get();
    hdr_com_spinlock_acquire(&cpu->runqueue_lock);
    com_thread_t *curr = cpu->thread;
    KASSERT(curr->sched_lock);
    com_thread_t *next = TAILQ_FIRST(&cpu->sched_queue);

    if (NULL == curr) {
        hdr_com_spinlock_release(&cpu->runqueue_lock);
        return;
    }

    if (NULL == next) {
        if (curr->runnable) {
            hdr_com_spinlock_release(&cpu->runqueue_lock);
            hdr_com_spinlock_release(&curr->sched_lock);
            return;
        }

        next = cpu->idle_thread;
    } else {
        TAILQ_REMOVE_HEAD(&cpu->sched_queue, threads);
    }

    if (curr->runnable && curr != cpu->idle_thread) {
        TAILQ_INSERT_TAIL(&cpu->sched_queue, curr, threads);
    }
    // TODO: ke does this, but it doesn't work here
    /*if (next != curr && next != cpu->idle_thread) {
        hdr_com_spinlock_release(&next->sched_lock);
    }*/
    hdr_com_spinlock_release(&cpu->runqueue_lock);

    arch_mmu_pagetable_t *prev_pt = NULL;
    arch_mmu_pagetable_t *next_pt = NULL;

    if (NULL != curr->proc) {
        prev_pt = curr->proc->page_table;
    }

    if (NULL != next->proc) {
        next_pt = next->proc->page_table;
    }

    if (NULL != next_pt && next_pt != prev_pt) {
        arch_mmu_switch(next_pt);

        if (NULL != curr && NULL != curr->proc && curr->proc->exited &&
            !curr->runnable) {
            /*hdr_com_spinlock_acquire(&curr->proc->pages_lock);
            arch_mmu_destroy_table(curr->proc->page_table);
            com_sys_thread_destroy(curr);
            hdr_com_spinlock_release(&curr->proc->pages_lock);*/
        } else if (NULL != curr) {
            ARCH_CONTEXT_SAVE_EXTRA(curr->xctx);
        }
    }

    ARCH_CONTEXT_RESTORE_EXTRA(next->xctx);

    cpu->thread = next;
    curr->cpu   = NULL;
    next->cpu   = cpu;

    // Restore kernel stack
    ARCH_CPU_SET_KERNEL_STACK(cpu, next->kernel_stack);

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
    hdr_com_spinlock_acquire(&hdr_arch_cpu_get_thread()->sched_lock);
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
    com_thread_t *curr = hdr_arch_cpu_get_thread();
    hdr_com_spinlock_acquire(&curr->sched_lock);
    hdr_com_spinlock_acquire(&hdr_arch_cpu_get()->runqueue_lock);
    if (NULL != curr->cpu) {
        curr->cpu = NULL;
    }
    curr->runnable   = false;
    curr->waiting_on = waiting_on;
    TAILQ_INSERT_TAIL(waiting_on, curr, threads);
    hdr_com_spinlock_release(&hdr_arch_cpu_get()->runqueue_lock);

    hdr_com_spinlock_release(cond);
    com_sys_sched_yield_nolock();

    // This point is reached AFTER the thread is notified
    hdr_com_spinlock_acquire(cond);
}

void com_sys_sched_notify(struct com_thread_tailq *waiters) {
    com_thread_t *curr_thread = hdr_arch_cpu_get_thread();
    arch_cpu_t   *currcpu     = hdr_arch_cpu_get();
    com_thread_t *next        = TAILQ_FIRST(waiters);
    hdr_com_spinlock_acquire(&curr_thread->sched_lock);

    if (NULL != next) {
        hdr_com_spinlock_acquire(&next->sched_lock);
        KASSERT(NULL == next->cpu);
        // KASSERT(next->waiting_on == waiters);
        TAILQ_REMOVE_HEAD(waiters, threads);
        next->waiting_on = NULL;
        hdr_com_spinlock_acquire(&currcpu->runqueue_lock);
        next->cpu = currcpu;
        TAILQ_INSERT_HEAD(&currcpu->sched_queue, next, threads);
        next->runnable = true;
        hdr_com_spinlock_release(&currcpu->runqueue_lock);
        hdr_com_spinlock_release(&next->sched_lock);
    }
    hdr_com_spinlock_release(&curr_thread->sched_lock);
}

void com_sys_sched_init(void) {
    arch_cpu_t *curr_cpu  = hdr_arch_cpu_get();
    curr_cpu->idle_thread = com_sys_thread_new(
        NULL,
        NULL /*(void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc())*/,
        ARCH_PAGE_SIZE,
        sched_idle);
    curr_cpu->idle_thread->ctx.rsp =
        (uintmax_t)curr_cpu->idle_thread->kernel_stack - 8;
    *(uint64_t *)curr_cpu->idle_thread->ctx.rsp = (uint64_t)sched_idle;
}
