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

#include <arch/cpu.h>
#include <kernel/com/io/log.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/syscall.h>
#include <kernel/platform/x86-64/msr.h>

extern com_intf_syscall_t Com_Sys_Syscall_Table[];

// #define X86_64_SYSCALL_LOG

void arch_syscall_handle(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
#ifndef DISABLE_LOGGING
    arch_context_t orig_ctx = *ctx;
#endif
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
#ifdef X86_64_SYSCALL_LOG
    KDEBUG("handling syscall %u(%u, %u, %u, %u) invoked at rip=%x (pid=%d, "
           "cpu=%d)",
           ctx->rax,
           ctx->rdi,
           ctx->rsi,
           ctx->rdx,
           ctx->rcx,
           ctx->rip,
           (NULL != curr_thread && NULL != curr_thread->proc)
               ? curr_thread->proc->pid
               : -1,
           (NULL != curr_thread && NULL != curr_thread->cpu)
               ? curr_thread->cpu->id
               : 100000);
#endif
    ARCH_CPU_GET_THREAD()->lock_depth = 0;
    ARCH_CPU_ENABLE_INTERRUPTS();
    com_intf_syscall_t handler = Com_Sys_Syscall_Table[ctx->rax];
    KASSERT(NULL != handler);
    com_syscall_ret_t ret =
        handler(ctx, ctx->rdi, ctx->rsi, ctx->rdx, ctx->rcx);

    if (!ret.discarded) {
        ctx->rax = ret.value;
        ctx->rdx = ret.err;
    }

    // KDEBUG("lock depth = %d", ARCH_CPU_GET()->lock_depth);
    // KDEBUG("sched lock = %d", ARCH_CPU_GET()->sched_lock);
    if (0 != curr_thread->lock_depth) {
        KDEBUG("in syscall %u(%u, %u, %u, %u) invoked at rip=%x (pid=%d, "
               "cpu=%d) lock depth check failed with value %d",
               orig_ctx.rax,
               orig_ctx.rdi,
               orig_ctx.rsi,
               orig_ctx.rdx,
               orig_ctx.rcx,
               orig_ctx.rip,
               (NULL != curr_thread && NULL != curr_thread->proc)
                   ? curr_thread->proc->pid
                   : -1,
               (NULL != curr_thread && NULL != curr_thread->cpu)
                   ? curr_thread->cpu->id
                   : 777, // NOTE: this means that the CPU was not set
               curr_thread->lock_depth);
    }
    KASSERT(0 == ARCH_CPU_GET_THREAD()->lock_depth);
    ARCH_CPU_DISABLE_INTERRUPTS();
    ARCH_CPU_GET_THREAD()->lock_depth = 1;
    // KDEBUG("syscall ret (%u, %u)", ret.value, ret.err);
}

COM_SYS_SYSCALL(arch_syscall_set_tls) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    uintptr_t ptr = COM_SYS_SYSCALL_ARG(uintptr_t, 1);

    ARCH_CPU_GET_THREAD()->xctx.fsbase = ptr;
    X86_64_MSR_WRITE(X86_64_MSR_FSBASE, ptr);

    return COM_SYS_SYSCALL_OK(0);
}
