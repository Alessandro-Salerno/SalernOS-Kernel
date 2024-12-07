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

#define ARCH_CONTEXT_THREAD_SET(ctx, stack, stack_size, entry) \
  ctx.cs     = 0x20 | 3;                                       \
  ctx.ss     = 0x18 | 3;                                       \
  ctx.rsp    = (uint64_t)stack + stack_size;                   \
  ctx.rip    = (uint64_t)entry;                                \
  ctx.rflags = (1ul << 1) | (1ul << 9) | (1ul << 21);

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
void arch_ctx_switch(arch_context_t *to, arch_context_t *from);
void arch_ctx_trampoline(arch_context_t *ctx);
