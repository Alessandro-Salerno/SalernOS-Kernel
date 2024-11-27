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

#include <arch/cpu.h>
#include <kernel/com/sys/interrupt.h>
#include <lib/printf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/context.h"
#include "com/panic.h"

bool com_sys_interrupt_set(bool status) {
  hdr_arch_cpu_interrupt_disable();
  bool old                      = hdr_arch_cpu_get()->intstatus;
  hdr_arch_cpu_get()->intstatus = status;

  if (status) {
    hdr_arch_cpu_interrupt_enable();
  }

  return old;
}

void com_sys_interrupt_register(uintmax_t      vec,
                                com_intf_isr_t func,
                                com_intf_eoi_t eoi) {
  bool orig_status = com_sys_interrupt_set(false);

  com_isr_t *isr = &hdr_arch_cpu_get()->isr[vec];
  isr->func      = func;
  isr->eoi       = eoi;
  isr->id        = (hdr_arch_cpu_get_id() << (sizeof(uintmax_t) * 8 / 2)) | vec;

  com_sys_interrupt_set(orig_status);
}

void com_sys_interrupt_isr(uintmax_t vec, arch_context_t *ctx) {
  com_sys_interrupt_set(false);
  com_isr_t *isr = &hdr_arch_cpu_get()->isr[vec];

  if (NULL == isr || NULL == isr->func) {
    com_panic(ctx, "isr not set for interrupt vector %u", vec);
  }

  isr->func(isr, ctx);

  if (NULL != isr->eoi) {
    isr->eoi(isr);
  }

  if (ARCH_CONTEXT_ISUSER(ctx)) {
    // TODO: userspace check
  }

  com_sys_interrupt_set(ARCH_CONTEXT_INTSTATUS(ctx));
}
