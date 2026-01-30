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
#include <kernel/com/io/log.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/syscall.h>
#include <kernel/platform/x86-64/msr.h>

void x86_64_syscall_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;

    ARCH_CPU_GET_THREAD()->lock_depth = 0;
    ARCH_CPU_ENABLE_INTERRUPTS();
    com_syscall_ret_t ret = com_sys_syscall_invoke(ctx->rax,
                                                   ctx,
                                                   ctx->rdi,
                                                   ctx->rsi,
                                                   ctx->rdx,
                                                   ctx->rcx,
                                                   0,
                                                   0,
                                                   ctx->rip);

    if (!ret.discarded) {
        ctx->rax = ret.value;
        ctx->rdx = ret.err;
    }

    KASSERT(0 == ARCH_CPU_GET_THREAD()->lock_depth);
    ARCH_CPU_DISABLE_INTERRUPTS();
    ARCH_CPU_GET_THREAD()->lock_depth = 1;
}

void x86_64_syscall_handle_sce(arch_context_t *ctx) {
    ARCH_CPU_GET_THREAD()->lock_depth = 0;
    ARCH_CPU_ENABLE_INTERRUPTS();
    com_syscall_ret_t ret = com_sys_syscall_invoke(ctx->rax,
                                                   ctx,
                                                   ctx->rdi,
                                                   ctx->rsi,
                                                   ctx->rdx,
                                                   ctx->r10,
                                                   ctx->r8,
                                                   ctx->r9,
                                                   ctx->rip);

    if (!ret.discarded) {
        ctx->rax = ret.value;
        ctx->rdx = ret.err;
    }

    KASSERT(0 == ARCH_CPU_GET_THREAD()->lock_depth);
    ARCH_CPU_DISABLE_INTERRUPTS();
    ARCH_CPU_GET_THREAD()->lock_depth = 1;
}

COM_SYS_SYSCALL(arch_syscall_set_tls) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    uintptr_t ptr = COM_SYS_SYSCALL_ARG(uintptr_t, 1);

    ARCH_CPU_GET_THREAD()->xctx.fsbase = ptr;
    X86_64_MSR_WRITE(X86_64_MSR_FSBASE, ptr);

    return COM_SYS_SYSCALL_OK(0);
}
