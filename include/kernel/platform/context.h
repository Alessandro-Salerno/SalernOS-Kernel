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

#include <arch/context.h>
#include <kernel/com/ipc/signal.h>

void arch_context_print(arch_context_t *ctx);
void arch_context_trampoline(arch_context_t *ctx);
void arch_context_fork_trampoline(void);

void arch_context_alloc_sigframe(com_sigframe_t **sframe,
                                 uintptr_t       *stackptr,
                                 arch_context_t  *ctxptr);
void arch_context_setup_sigframe(com_sigframe_t *sframe, arch_context_t *ctx);
void arch_context_setup_sigrestore(uintptr_t *stackptr, void *restorer);
void arch_context_signal_trampoline(com_sigframe_t *sframe,
                                    arch_context_t *ctx,
                                    uintptr_t       stack,
                                    void           *handler,
                                    int             sig);
void arch_context_restore_sigframe(com_sigframe_t **sframeptr,
                                   arch_context_t  *ctx);
