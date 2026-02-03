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

#include <arch/cpu.h>
#include <lib/str.h>
#include <lib/util.h>
#include <stddef.h>
#include <vendor/printf.h>

void kinitlog(const char *category, const char *color) {
    struct arch_cpu   *curr_cpu    = ARCH_CPU_GET();
    struct com_thread *curr_thread = ARCH_CPU_GET_THREAD();

#if CONFIG_LOG_ALLOW_COLORS
    KRESET_COLOR();
    printf("%s[%s  ", color, category);
#else
    (void)color;
    printf("[%s  ", category);
#endif

    ssize_t cat_len = kstrlen(category);
    ssize_t diff    = CONFIG_LOG_SEP_LEN - cat_len;
    for (ssize_t i = 0; i < diff; i++) {
        com_io_log_putc_nolock(' ');
    }

    if (NULL == curr_cpu) {
        printf("**NOCPU**");
        goto log;
    }

    printf("cpu=%d/", curr_cpu->id);

    if (NULL == curr_thread) {
        printf("KERNEL");
        goto log;
    }

    printf("tid=%d/", curr_thread->tid);

    if (NULL == curr_thread->proc) {
        printf("KERNEL");
        goto log;
    }

    printf("pid=%d", curr_thread->proc->pid);
log:
    printf("]\t");
}
