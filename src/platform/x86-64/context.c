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

#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/sched.h>
#include <kernel/platform/context.h>
#include <kernel/platform/x86-64/context.h>
#include <lib/usercopy.h>
#include <vendor/printf.h>

void arch_context_print(arch_context_t *ctx) {
    printf("\ncr2: %p  gs: %p\nrax: %p  fs: %p\nrbx: %p  es: "
           "%p\nrcx: %p  ds: %p\nrdx: %p  cs: %p\n r8: %p  ss: "
           "%p\n r9: %p r10: %p\nr11: %p r12: %p\nr13: %p "
           "r14: %p\nr15: %p rdi: %p\nrsi: %p rbp: %p\nrip: "
           "%p rsp: %p\nerr: %p rfl: %p\n\n",
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

    // printf("instruction at (%%rip):\n\t");
    // printf("%p %p\n", *(uint64_t *)ctx->rip, *(((uint64_t *)ctx->rip) + 1));
}

// The FPU state goes right after the frame, aligned for fxsave/fxrstor
#define SIGFRAME_FPU_OFFSET ((sizeof(com_sigframe_t) + 15) & ~(size_t)15)
#define SIGFRAME_SIZE       (SIGFRAME_FPU_OFFSET + sizeof(struct _fpstate))

int arch_context_alloc_sigframe(com_sigframe_t **sframe,
                                uintptr_t       *stackptr,
                                arch_context_t  *ctx) {
    uint64_t stack = (uint64_t)ctx->rsp;
    stack -= 128;
    stack &= ~0xFL;
    stack -= SIGFRAME_SIZE;
    *sframe   = (void *)stack;
    *stackptr = stack;

    return ARCH_MMU_RANGE_IS_USER(*sframe, SIGFRAME_SIZE) ? 0 : EFAULT;
}

int arch_context_setup_sigframe(com_sigframe_t *sframe,
                                com_sigframe_t *uframe,
                                arch_context_t *ctx) {
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

    struct _fpstate *ufpu = (void *)((uintptr_t)uframe + SIGFRAME_FPU_OFFSET);
    sframe->uc.uc_mcontext.fpregs = ufpu;
    // A switch during the copy saves the same live state again
    arch_context_extra_t *xctx = &ARCH_CPU_GET_THREAD()->xctx;
    ARCH_CONTEXT_SAVE_EXTRA(*xctx);
    return kmemcpy_to_user(ufpu, xctx->fpu, sizeof(xctx->fpu));
}

int arch_context_setup_sigrestore(uintptr_t *stackptr, void *restorer) {
    uint64_t stack = *stackptr - 8;
    int      ret = kmemcpy_to_user((void *)stack, &restorer, sizeof(restorer));
    if (0 == ret) {
        *stackptr = stack;
    }
    return ret;
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

    uint32_t mxcsr = 0x1f80;
    asm volatile("fninit\n\tldmxcsr %0" : : "m"(mxcsr));
}

int arch_context_restore_sigframe(com_sigframe_t *sframe, arch_context_t *ctx) {
    com_sigframe_t *uframe = (void *)ctx->rsp;
    int             ret    = kmemcpy_from_user(sframe, uframe, sizeof(*sframe));
    if (0 != ret) {
        return ret;
    }

    struct _fpstate *ufpu = sframe->uc.uc_mcontext.fpregs;
    if (NULL == ufpu) {
        return EFAULT;
    }
    // Reserved MXCSR bits would make fxrstor fault
    struct _fpstate fpu __attribute__((aligned(16)));
    asm volatile("fxsave %0" : "=m"(fpu));
    uint32_t mxcsr_mask = (0 != fpu.mxcr_mask) ? fpu.mxcr_mask : 0xffbf;
    ret                 = kmemcpy_from_user(&fpu, ufpu, sizeof(fpu));
    if (0 != ret) {
        return ret;
    }
    fpu.mxcsr &= mxcsr_mask;
    asm volatile("fxrstor %0" : : "m"(fpu));

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

    // Do not let user code set privileged flags (e.g., IOPL)
    ctx->rflags = (ctx->rflags & 0x40DD5) | 0x202;
    return 0;
}

void x86_64_ctx_prepare(arch_context_t *ctx, arch_context_t *frame) {
    uintptr_t base = ((uintptr_t)frame - sizeof(arch_context_message_t) -
                      sizeof(frame)) &
                     ~(uintptr_t)15;
    *(arch_context_t **)(base + sizeof(arch_context_message_t)) = frame;
    uintptr_t *stack                                            = (void *)base;
    *--stack = (uintptr_t)arch_context_fork_trampoline;
    *--stack = base;
    ctx->rsp = (uintptr_t)stack;
}

void x86_64_ctx_first_entry(arch_context_message_t *message,
                            arch_context_t         *frame) {
    com_sys_sched_switch_finish(*message);
    arch_context_trampoline(frame);
    __builtin_unreachable();
}
