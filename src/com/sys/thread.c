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
#include <vendor/tailq.h>

static pid_t NextTid = 0;

static com_thread_t *new_thread(com_proc_t *proc, arch_context_t ctx) {
    com_thread_t *thread = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
    thread->proc         = proc;
    thread->runnable     = true;
    thread->exited       = false;
    thread->ctx          = ctx;
    thread->lock_depth   = 1;
    thread->sched_lock   = KSPINLOCK_NEW();
    thread->kernel_stack = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc()) +
                           ARCH_PAGE_SIZE;
    ARCH_CONTEXT_INIT_EXTRA(thread->xctx);
    thread->tid = __atomic_fetch_add(&NextTid, 1, __ATOMIC_RELAXED);

    thread->waiting_on = NULL;
    thread->ctime      = ARCH_CPU_GET_TIMESTAMP();

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

com_thread_t *com_sys_thread_new_kernel(com_proc_t *proc, void *entry) {
    arch_context_t ctx = {0};
    com_thread_t  *t   = new_thread(proc, ctx);
    ARCH_CONTEXT_THREAD_SET_KERNEL(t->ctx, t->kernel_stack, entry);
    return t;
}

void com_sys_thread_exit(com_thread_t *thread) {
    kspinlock_acquire(&thread->sched_lock);
    com_sys_thread_exit_nolock(thread);
    kspinlock_release(&thread->sched_lock);
}

void com_sys_thread_exit_nolock(com_thread_t *thread) {
    thread->runnable = false;
    thread->exited   = true;
    if (NULL != thread->cpu && thread->cpu != ARCH_CPU_GET()) {
        ARCH_CPU_SEND_IPI(thread->cpu, ARCH_CPU_IPI_RESCHEDULE);
    }
}

void com_sys_thread_destroy(com_thread_t *thread) {
    thread->runnable = false;
    if (NULL != thread->proc) {
        COM_SYS_PROC_RELEASE(thread->proc);
    }
    com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(thread->kernel_stack) -
                    ARCH_PAGE_SIZE);
    com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(thread));
}

void com_sys_thread_ready_nolock(com_thread_t *thread) {
    arch_cpu_t *curr_cpu = x86_64_smp_get_random();
    kspinlock_acquire(&curr_cpu->runqueue_lock);
    TAILQ_INSERT_TAIL(&curr_cpu->sched_queue, thread, threads);
    thread->runnable   = true;
    thread->waiting_on = NULL;
    kspinlock_release(&curr_cpu->runqueue_lock);
    KDEBUG("thread with tid=%zu is now runnable on cpu %zu",
           thread->tid,
           curr_cpu->id);
}

void com_sys_thread_ready(com_thread_t *thread) {
    kspinlock_acquire(&thread->sched_lock);
    com_sys_thread_ready_nolock(thread);
    kspinlock_release(&thread->sched_lock);
}
