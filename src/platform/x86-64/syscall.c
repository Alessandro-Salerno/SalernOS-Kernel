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

void arch_syscall_handle(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
    // KDEBUG("handling syscall %u(%u, %u, %u, %u) invoked at rip=%x",
    //        ctx->rax,
    //        ctx->rdi,
    //        ctx->rsi,
    //        ctx->rdx,
    //        ctx->rip);
    com_intf_syscall_t handler = Com_Sys_Syscall_Table[ctx->rax];
    com_syscall_ret_t  ret =
        handler(ctx, ctx->rdi, ctx->rsi, ctx->rdx, ctx->rcx);
    ctx->rax = ret.value;
    ctx->rdx = ret.err;
    // KDEBUG("syscall ret (%u, %u)", ret.value, ret.err);
}

com_syscall_ret_t arch_syscall_set_tls(arch_context_t *ctx,
                                       uintmax_t       ptr,
                                       uintmax_t       unused1,
                                       uintmax_t       unused2,
                                       uintmax_t       unused3) {
    (void)ctx;
    (void)unused1;
    (void)unused2;
    (void)unused3;
    hdr_arch_cpu_get_thread()->xctx.fsbase = ptr;
    hdr_x86_64_msr_write(X86_64_MSR_FSBASE, ptr);
    return (com_syscall_ret_t){.value = 0, .err = 0};
}
