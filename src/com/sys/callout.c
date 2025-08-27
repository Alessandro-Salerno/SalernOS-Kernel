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

#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/sched.h>
#include <lib/util.h>

// CREDIT: vloxei64/ke
// CREDIT: FreeBSD

static void enqueue_callout(com_callout_queue_t *cpu_callout,
                            com_callout_t       *callout) {
    // Add callout to the queue so that it is ordered
    bool           added = false;
    com_callout_t *curr  = TAILQ_FIRST(&cpu_callout->queue);
    while (NULL != curr) {
        if (callout->ns < curr->ns) {
            TAILQ_INSERT_BEFORE(curr, callout, queue);
            added = true;
            break;
        }
        curr = TAILQ_NEXT(curr, queue);
    }

    // If the new callout was not added by the previous block, it means that its
    // delay is higher than that of any other callout in the queue, so it needs
    // to be placed at the end
    if (!added) {
        TAILQ_INSERT_TAIL(&cpu_callout->queue, callout, queue);
    }
}

uintmax_t com_sys_callout_get_time(void) {
    com_callout_queue_t *callout = &ARCH_CPU_GET()->callout;
    kspinlock_acquire(&callout->lock);
    uintmax_t time = callout->ns;
    kspinlock_release(&callout->lock);
    return time;
}

void com_sys_callout_run(void) {
    arch_cpu_t          *curr_cpu    = ARCH_CPU_GET();
    com_callout_queue_t *cpu_callout = &curr_cpu->callout;

    kspinlock_acquire(&cpu_callout->lock);
    cpu_callout->ns += ARCH_TIMER_NS;

    while (true) {
        com_callout_t *callout = TAILQ_FIRST(&cpu_callout->queue);

        if (NULL == callout || callout->ns > cpu_callout->ns) {
            break;
        }

        TAILQ_REMOVE_HEAD(&cpu_callout->queue, queue);
        callout->reuse = false;

        kspinlock_release(&cpu_callout->lock);
        callout->handler(callout);
        kspinlock_acquire(&cpu_callout->lock);

        if (!callout->reuse) {
            com_mm_slab_free(callout, sizeof(com_callout_t));
        }
    }

    bool resched = false;
    if (cpu_callout->ns >= cpu_callout->next_preempt) {
        resched = true;
        cpu_callout->next_preempt += ARCH_SCHED_NS;
    }

    kspinlock_release(&cpu_callout->lock);

    if (resched) {
        com_sys_sched_yield();
    }
}

void com_sys_callout_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
    (void)ctx;
    com_sys_callout_run();
}

void com_sys_callout_reschedule_at(com_callout_t *callout, uintmax_t ns) {
    com_callout_queue_t *cpu_callout = &ARCH_CPU_GET()->callout;
    callout->reuse                   = true;
    callout->ns                      = ns;

    kspinlock_acquire(&cpu_callout->lock);
    enqueue_callout(cpu_callout, callout);
    kspinlock_release(&cpu_callout->lock);
}

void com_sys_callout_reschedule(com_callout_t *callout, uintmax_t delay) {
    com_sys_callout_reschedule_at(callout, ARCH_CPU_GET()->callout.ns + delay);
}

void com_sys_callout_add_at(com_callout_intf_t handler,
                            void              *arg,
                            uintmax_t          ns) {
    com_callout_queue_t *cpu_callout = &ARCH_CPU_GET()->callout;

    com_callout_t *new = com_mm_slab_alloc(sizeof(com_callout_t));
    new->handler       = handler;
    new->arg           = arg;
    new->ns            = ns;
    new->reuse         = false;

    kspinlock_acquire(&cpu_callout->lock);
    enqueue_callout(cpu_callout, new);
    kspinlock_release(&cpu_callout->lock);
}

void com_sys_callout_add(com_callout_intf_t handler,
                         void              *arg,
                         uintmax_t          delay) {
    com_sys_callout_add_at(handler, arg, ARCH_CPU_GET()->callout.ns + delay);
}
