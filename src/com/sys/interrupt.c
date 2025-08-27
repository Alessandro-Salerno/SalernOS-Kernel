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
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/panic.h>
#include <lib/printf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static com_isr_t InterruptTable[ARCH_NUM_INTERRUPTS] = {0};

void com_sys_interrupt_register(uintmax_t      vec,
                                com_intf_isr_t func,
                                com_intf_eoi_t eoi) {
    KDEBUG("registering handler at %p for vector %u (%p)", func, vec, vec);
    com_isr_t *isr = &InterruptTable[vec];
    isr->func      = func;
    isr->eoi       = eoi;
    isr->vec       = vec;
}

void com_sys_interrupt_isr(uintmax_t vec, arch_context_t *ctx) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL != curr_thread) {
        curr_thread->lock_depth = 1;
    }

    com_isr_t *isr = &InterruptTable[vec];

    if (NULL == isr) {
        com_sys_panic(ctx, "isr not set for interrupt vector %u", vec);
    }

    if (NULL != isr->eoi) {
        isr->eoi(isr);
    }

    if (NULL != isr->func) {
        isr->func(isr, ctx);
    }

    KASSERT(curr_thread == ARCH_CPU_GET_THREAD());
    KASSERT(curr_thread->tid == ARCH_CPU_GET_THREAD()->tid);

    if (ARCH_CONTEXT_ISUSER(ctx)) {
        com_ipc_signal_dispatch(ctx, curr_thread);
    }

    if (NULL != curr_thread) {
        KASSERT(1 == curr_thread->lock_depth);
        curr_thread->lock_depth = 0;
    }
}
