/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2025 Alessandro Salerno

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
#include <kernel/com/sys/signal.h>
#include <kernel/platform/x86-64/context.h>
#include <lib/printf.h>

void arch_context_print(arch_context_t *ctx) {
    kprintf("\ncr2: %x  gs: %x\nrax: %x  fs: %x\nrbx: %x  es: "
            "%x\nrcx: %x  ds: %x\nrdx: %x  cs: %x\n r8: %x  ss: "
            "%x\n r9: %x r10: %x\nr11: %x r12: %x\nr13: %x "
            "r14: %x\nr15: %x rdi: %x\nrsi: %x rbp: %x\nrip: "
            "%x rsp: %x\nerr: %x rfl: %x\n\n",
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

    // kprintf("instruction at (%%rip):\n\t");
    // kprintf("%x %x\n", *(uint64_t *)ctx->rip, *(((uint64_t *)ctx->rip) + 1));
}

void arch_context_alloc_sigframe(com_sigframe_t **sframe,
                                 uintptr_t       *stackptr,
                                 arch_context_t  *ctx) {
    uint64_t stack = (uint64_t)ctx->rsp;
    stack -= 128;
    stack &= ~0xFL;
    stack -= sizeof(com_sigframe_t);
    stack &= ~0xFL;
    *sframe   = (void *)stack;
    *stackptr = stack;
}

void arch_context_setup_sigframe(com_sigframe_t *sframe, arch_context_t *ctx) {
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R8]  = ctx->r8;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R9]  = ctx->r9;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R10] = ctx->r10;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R11] = ctx->r11;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R12] = ctx->r12;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R13] = ctx->r13;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R14] = ctx->r14;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R15] = ctx->r15;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RDI] = ctx->rdi;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RSI] = ctx->rsi;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RBP] = ctx->rbp;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RBX] = ctx->rbx;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RDX] = ctx->rdx;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RAX] = ctx->rax;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RCX] = ctx->rcx;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RSP] = ctx->rsp;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RIP] = ctx->rip;
    sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_EFL] = ctx->rflags;
}

void arch_context_setup_sigrestore(com_sigframe_t *sframe,
                                   uintptr_t      *stackptr,
                                   void           *restorer) {
    uint64_t stack = *stackptr;
    stack -= 8;
    *(uint64_t *)stack = (uint64_t)restorer;
    *stackptr          = stack;
}

void arch_context_signal_trampoline(com_sigframe_t *sframe,
                                    arch_context_t *ctx,
                                    uintptr_t       stack,
                                    void           *handler,
                                    int             sig) {
    ctx->rip = (uint64_t)handler;
    ctx->rsp = (uint64_t)stack;
    ctx->rdi = sig;
    ctx->rsi = (uint64_t)(&sframe->info);
    ctx->rdx = (uint64_t)(&sframe->uc);
}

void arch_context_restore_sigframe(com_sigframe_t **sframeptr,
                                   arch_context_t  *ctx) {
    com_sigframe_t *sframe = (void *)ctx->rsp;
    ctx->r8     = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R8];
    ctx->r9     = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R9];
    ctx->r10    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R10];
    ctx->r11    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R11];
    ctx->r12    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R12];
    ctx->r13    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R13];
    ctx->r14    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R14];
    ctx->r15    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_R15];
    ctx->rdi    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RDI];
    ctx->rsi    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RSI];
    ctx->rbp    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RBP];
    ctx->rbx    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RBX];
    ctx->rdx    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RDX];
    ctx->rax    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RAX];
    ctx->rcx    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RCX];
    ctx->rsp    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RSP];
    ctx->rip    = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_RIP];
    ctx->rflags = sframe->uc.uc_mcontext.gregs[X86_64_CONTEXT_REG_EFL];
    *sframeptr  = sframe;
}
