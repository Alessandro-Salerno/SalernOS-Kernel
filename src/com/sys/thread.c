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
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/x86-64/smp.h>
#include <stdint.h>
#include <vendor/tailq.h>

com_thread_t *com_sys_thread_new(com_proc_t *proc,
                                 void       *stack,
                                 uintmax_t   stack_size,
                                 void       *entry) {
    arch_context_t ctx = {0};
    ARCH_CONTEXT_THREAD_SET(ctx, stack, stack_size, entry);

    com_thread_t *thread =
        (com_thread_t *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
    thread->proc       = proc;
    thread->runnable   = true;
    thread->ctx        = ctx;
    thread->lock_depth = 1;
    thread->kernel_stack =
        (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc()) + ARCH_PAGE_SIZE;
    ARCH_CONTEXT_INIT_EXTRA(thread->xctx);

    return thread;
}

void com_sys_thread_destroy(com_thread_t *thread) {
    thread->runnable   = false;
    thread->waiting_on = NULL;
    com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(thread->kernel_stack) -
                    ARCH_PAGE_SIZE);
    com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(thread));
}

void com_sys_thread_ready(com_thread_t *thread) {
    arch_cpu_t *curr_cpu = x86_64_smp_get_random();
    hdr_com_spinlock_acquire(&curr_cpu->sched_lock);
    TAILQ_INSERT_TAIL(&curr_cpu->sched_queue, thread, threads);
    thread->runnable = true;
    hdr_com_spinlock_release(&curr_cpu->sched_lock);
    KDEBUG("thread is now runnable");
}
