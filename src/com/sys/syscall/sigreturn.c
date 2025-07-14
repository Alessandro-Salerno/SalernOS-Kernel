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

#include <arch/context.h>
#include <arch/cpu.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/signal.h>
#include <kernel/com/sys/syscall.h>

// SYSCALL: sigreturn()
COM_SYS_SYSCALL(com_sys_syscall_sigreturn) {
    COM_SYS_SYSCALL_UNUSED_START(1);

    arch_context_t *ctx = COM_SYS_SYSCALL_CONTEXT();
    com_sigframe_t *sframe;
    ARCH_CONTEXT_RESTORE_SIGFRAME(&sframe, ctx);
    ARCH_CPU_GET_THREAD()->masked_signals = sframe->uc.uc_sigmask.sig[0];

    return COM_SYS_SYSCALL_OK(0);
}
