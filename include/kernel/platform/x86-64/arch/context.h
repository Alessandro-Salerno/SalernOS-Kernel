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

#pragma once

#include <stdint.h>

#define ARCH_CONTEXT_INTSTATUS(x) ((x)->rflags & 0x200 ? true : false)
#define ARCH_CONTEXT_ISUSER(x)    (0x23 == (x)->cs)

#define ARCH_CONTEXT_THREAD_SET(src, stack, stack_size, entry) \
  src.cs     = 0x20 | 3;                                       \
  src.ss     = 0x18 | 3;                                       \
  src.rsp    = (uint64_t)stack + stack_size;                   \
  src.rip    = (uint64_t)entry;                                \
  src.rflags = (1ul << 1) | (1ul << 9) | (1ul << 21);

#define ARCH_CONTEXT_FORK(new_thread, orig_ctx)                          \
  new_thread->ctx.rsp = (uint64_t)new_thread->kernel_stack;              \
  new_thread->ctx.rsp -= sizeof(arch_context_t);                         \
  {                                                                      \
    arch_context_t *new_context = (void *)new_thread->ctx.rsp;           \
    *new_context                = (orig_ctx); /* copy the old context */ \
  }                                                                      \
  new_thread->ctx.rsp -= 8;                                              \
  *(uint64_t *)new_thread->ctx.rsp = (uint64_t)arch_ctx_fork_trampoline; \
  new_thread->ctx.rax              = 0;                                  \
  new_thread->ctx.rdx              = 0;

#define ARCH_CONTEXT_COPY(dst, src) \
  (dst)->rax = (src)->rax;          \
  (dst)->rbx = (src)->rbx;          \
  (dst)->rcx = (src)->rcx;          \
  (dst)->rdx = (src)->rdx;          \
  (dst)->r8  = (src)->r8;           \
  (dst)->r9  = (src)->r9;           \
  (dst)->r10 = (src)->r10;          \
  (dst)->r11 = (src)->r11;          \
  (dst)->r12 = (src)->r12;          \
  (dst)->r13 = (src)->r13;          \
  (dst)->r14 = (src)->r14;          \
  (dst)->r15 = (src)->r15;          \
  (dst)->rdi = (src)->rdi;          \
  (dst)->rsi = (src)->rsi;          \
  (dst)->rbp = (src)->rbp;          \
  (dst)->rip = (src)->rip;          \
  (dst)->rsp = (src)->rsp;

#define ARCH_CONTEXT_SWITCH(to, from) x86_64_ctx_switch((to), (from))

#define ARCH_CONTEXT_RESTORE_TLC(ctx) \
  hdr_x86_64_msr_write(X86_64_MSR_KERNELGSBASE, (uint64_t)(ctx)->gs)

typedef struct {
  uint64_t cr2;
  uint64_t gs;
  uint64_t fs;
  uint64_t es;
  uint64_t ds;
  uint64_t rax;
  uint64_t rbx;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rbp;
  uint64_t error;
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
} __attribute__((packed)) arch_context_t;

void arch_ctx_print(arch_context_t *ctx);
void x86_64_ctx_switch(arch_context_t *to, arch_context_t *from);
void x86_64_ctx_test_trampoline(void);
void arch_ctx_trampoline(arch_context_t *ctx);
void arch_ctx_fork_trampoline(void);
