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

#pragma once

#include <kernel/platform/x86-64/context.h>
#include <kernel/platform/x86-64/msr.h>
#include <lib/mem.h>
#include <stdint.h>

#define ARCH_CONTEXT_INTSTATUS(x) ((x)->rflags & 0x200 ? true : false)
#define ARCH_CONTEXT_ISUSER(x)    (0x23 == (x)->cs)

#define ARCH_CONTEXT_THREAD_SET(src, stack, stack_size, entry) \
    (src).cs     = 0x20 | 3;                                   \
    (src).ss     = 0x18 | 3;                                   \
    (src).rsp    = (uint64_t)(stack) + (stack_size);           \
    (src).rip    = (uint64_t)(entry);                          \
    (src).rflags = (1ul << 1) | (1ul << 9) | (1ul << 21);

#define ARCH_CONTEXT_THREAD_SET_KERNEL(src, stack_end, entry)            \
    (src).cs     = 0x08;                                                 \
    (src).ss     = 0x10;                                                 \
    (src).rip    = (uint64_t)(entry);                                    \
    (src).rsp    = (uint64_t)(stack_end) - 8;                            \
    (src).rflags = (1ul << 1) | (1ul << 9) | (1ul << 21);                \
    (src).rsp -= sizeof(arch_context_t);                                 \
    {                                                                    \
        arch_context_t *new_context = (void *)(src).rsp;                 \
        *new_context                = (src);                             \
        (src).rsp -= 8;                                                  \
        *(uint64_t *)(src).rsp = (uint64_t)arch_context_fork_trampoline; \
    }

#define ARCH_CONTEXT_FORK(new_thread, orig_ctx)                              \
    new_thread->ctx.rsp = (uint64_t)new_thread->kernel_stack;                \
    new_thread->ctx.rsp -= sizeof(arch_context_t);                           \
    {                                                                        \
        arch_context_t *new_context = (void *)new_thread->ctx.rsp;           \
        *new_context                = (orig_ctx); /* copy the old context */ \
        new_thread->ctx.rsp -= 8;                                            \
        *(uint64_t *)new_thread->ctx.rsp = (uint64_t)                        \
            arch_context_fork_trampoline;                                    \
        new_context->rax = 0;                                                \
        new_context->rdx = 0;                                                \
    }

#define ARCH_CONTEXT_CLONE(new_thread, new_ip, new_sp)              \
    new_thread->ctx.rsp = (uint64_t)new_thread->kernel_stack;       \
    new_thread->ctx.rsp -= sizeof(arch_context_t);                  \
    {                                                               \
        arch_context_t *new_context = (void *)new_thread->ctx.rsp;  \
        *new_context                = (arch_context_t){0};          \
        new_thread->ctx.rsp -= 8;                                   \
        *(uint64_t *)new_thread->ctx.rsp = (uint64_t)               \
            arch_context_fork_trampoline;                           \
        ARCH_CONTEXT_THREAD_SET((*new_context), new_sp, 0, new_ip); \
    }

#define ARCH_CONTEXT_SWITCH(to, from, from_sched_lock) \
    x86_64_ctx_switch((to), (from), (from_sched_lock))

#define ARCH_CONTEXT_RESTORE_TLC(ctx) \
    X86_64_MSR_WRITE(X86_64_MSR_KERNELGSBASE, (uint64_t)(ctx)->gs)

#define ARCH_CONTEXT_SAVE_EXTRA(xctx) \
    asm volatile("fxsave (%0)" : : "r"(&((xctx).fpu)) : "memory")

#define ARCH_CONTEXT_RESTORE_EXTRA(xctx)                            \
    asm volatile("fxrstor (%0)" : : "r"(&((xctx).fpu)) : "memory"); \
    X86_64_MSR_WRITE(X86_64_MSR_FSBASE, (xctx).fsbase);

#define ARCH_CONTEXT_INIT_EXTRA(xctx)                      \
    {                                                      \
        kmemset((xctx).fpu, sizeof((xctx).fpu), 0);        \
        uint16_t fcw   = 0x037f;                           \
        uint16_t ftw   = 0xffff;                           \
        uint32_t mxcsr = 0x1f80;                           \
        kmemcpy((xctx).fpu + 0x00, &fcw, sizeof(fcw));     \
        kmemcpy((xctx).fpu + 0x08, &ftw, sizeof(ftw));     \
        kmemcpy((xctx).fpu + 0x18, &mxcsr, sizeof(mxcsr)); \
        (xctx).fsbase = 0;                                 \
        (xctx).gsbase = 0;                                 \
    }

#define ARCH_CONTEXT_FORK_EXTRA(child_xctx, parent_xctx) \
    {                                                    \
        kmemcpy((child_xctx).fpu,                        \
                (parent_xctx).fpu,                       \
                sizeof((parent_xctx).fpu));              \
        (child_xctx).fsbase = (parent_xctx).fsbase;      \
    }

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

typedef struct {
    uint8_t  fpu[512] __attribute__((aligned(16)));
    uint64_t fsbase;
    uint64_t gsbase;
} __attribute__((packed)) arch_context_extra_t;

void x86_64_ctx_switch(arch_context_t *to,
                       arch_context_t *from,
                       int            *from_sched_lock);
void x86_64_ctx_test_trampoline(void);
