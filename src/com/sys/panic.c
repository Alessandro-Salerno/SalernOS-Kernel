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
#include <kernel/com/io/log.h>
#include <kernel/com/io/term.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/panic.h>
#include <kernel/platform/context.h>
#include <lib/printf.h>
#include <stdarg.h>
#include <stddef.h>

__attribute__((noreturn)) void
com_sys_panic(arch_context_t *ctx, const char *fmt, ...) {
    ARCH_CPU_DISABLE_INTERRUPTS();
    com_io_term_panic();
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    kprintf("kernel panic on cpu %u\n", ARCH_CPU_GET_ID());

    if (NULL != fmt) {
        va_list args;
        va_start(args, fmt);
        kvprintf(fmt, args);
        va_end(args);
        kprintf("\n\n");
    }

    size_t used_mem, free_mem, rsv_mem, sys_mem, mem_size;
    com_mm_pmm_get_info(&used_mem, &free_mem, &rsv_mem, &sys_mem, &mem_size);
    kprintf("used memory: %u byte(s)\n", used_mem);
    kprintf("free memory: %u byte(s)\n", free_mem);
    kprintf("reserved memory: %u byte(s)\n", rsv_mem);
    kprintf("system memory: %u byte(s)\n", sys_mem);
    kprintf("total memory: %u byte(s)\n", mem_size);
    kprintf("\n");

    if (NULL == ctx && NULL != curr_thread) {
        kprintf("note: cpu context data refers to kernel stack for tid=%d, "
                "since context was not provided\n",
                curr_thread->tid);
        ctx = &curr_thread->ctx;
    }

    if (NULL != ctx) {
        arch_context_print(ctx);
    }

    while (1) {
        ARCH_CPU_DISABLE_INTERRUPTS();
        ARCH_CPU_HALT();
    }
}
