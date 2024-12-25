/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2024 Alessandro Salerno

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**********************************************************************/

#include <arch/context.h>
#include <lib/printf.h>

void arch_context_print(arch_context_t *ctx) {
  kprintf("\ncr2: %x  gs: %x\nrax: %x  fs: %x\nrbx: %x  es: "
          "%x\nrcx: %x  ds: %x\nrdx: %x  cs: %x\n r8: %x  ss: "
          "%x\n r9: %x r10: %x\nr11: %x r12: %x\nr13: %x "
          "r14: %x\nr15: %x rdi: %x\nrsi: %x rbp: %x\nrip: "
          "%x rsp: %x\nerr: %x rfl: %x\n",
          ctx->cr2,
          ctx->gs,
          ctx->rax,
          ctx->fs,
          ctx->rbx,
          ctx->es,
          ctx->rcx,
          ctx->ds,
          ctx->rdx,
          ctx->cs,
          ctx->r8,
          ctx->ss,
          ctx->r9,
          ctx->r10,
          ctx->r11,
          ctx->r12,
          ctx->r13,
          ctx->r14,
          ctx->r15,
          ctx->rdi,
          ctx->rsi,
          ctx->rbp,
          ctx->rip,
          ctx->rsp,
          ctx->error,
          ctx->rflags);
}
