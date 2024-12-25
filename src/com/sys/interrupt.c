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
#include <com/panic.h>
#include <kernel/com/log.h>
#include <kernel/com/sys/interrupt.h>
#include <lib/printf.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

  KDEBUG("registering handler at %x for vector %u (%x)", func, vec, vec);
  com_isr_t *isr = &hdr_arch_cpu_get()->isr[vec];
  isr->func      = func;
  isr->eoi       = eoi;
  isr->id        = (hdr_arch_cpu_get_id() << (sizeof(uintmax_t) * 8 / 2)) | vec;

  com_sys_interrupt_set(orig_status);
}

void com_sys_interrupt_isr(uintmax_t vec, arch_context_t *ctx) {
  // TODO: check if this should be cpu->intstatus = false
  //        CHECK EVERYWHERE
  com_sys_interrupt_set(false);
  com_isr_t *isr = &hdr_arch_cpu_get()->isr[vec];

  if (NULL == isr) {
    com_panic(ctx, "isr not set for interrupt vector %u", vec);
  }

  if (NULL != isr->func) {
    isr->func(isr, ctx);
  }

  if (NULL != isr->eoi) {
    isr->eoi(isr);
  }

  ARCH_CONTEXT_RESTORE_TLC(ctx);
  com_sys_interrupt_set(ARCH_CONTEXT_INTSTATUS(ctx));
}
