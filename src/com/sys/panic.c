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
#include <stdarg.h>
#include <stddef.h>
#include <vendor/printf.h>

__attribute__((noreturn)) void
com_sys_panic(arch_context_t *ctx, const char *fmt, ...) {
    ARCH_CPU_DISABLE_INTERRUPTS();
    ARCH_CPU_BROADCAST_IPI(ARCH_CPU_IPI_PANIC);
    com_io_term_panic();
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    printf("kernel panic on cpu %zu\n", ARCH_CPU_GET_ID());

    if (NULL != fmt) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        printf("\n\n");
    }

    com_pmm_stats_t memstats;
    com_mm_pmm_get_stats(&memstats);
    printf("used memory: %zu byte(s)\n", memstats.used);
    printf("free memory: %zu byte(s)\n", memstats.free);
    printf("reserved memory: %zu byte(s)\n", memstats.reserved);
    printf("system memory: %zu byte(s)\n", memstats.usable);
    printf("total memory: %zu byte(s)\n", memstats.total);
    printf("\n");

    if (NULL == ctx && NULL != curr_thread) {
        printf("note: cpu context data refers to kernel stack for tid=%d, "
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
