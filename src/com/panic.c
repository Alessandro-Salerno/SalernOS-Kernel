/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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
#include <kernel/com/log.h>
#include <kernel/com/panic.h>
#include <lib/printf.h>
#include <stdarg.h>
#include <stddef.h>

#include "com/sys/interrupt.h"

__attribute__((noreturn)) void
com_panic(arch_context_t *ctx, const char *fmt, ...) {
  kprintf("kernel panic on cpu %u\n", hdr_arch_cpu_get_id());

  if (NULL != fmt) {
    va_list args;
    va_start(args, fmt);
    kvprintf(fmt, args);
    va_end(args);
    kprintf("\n");
  }

  if (NULL != ctx) {
    arch_ctx_print(ctx);
  }

  com_sys_interrupt_set(false);
  while (1)
    ;
}