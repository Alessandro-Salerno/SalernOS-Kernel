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
#include <stdint.h>
#include <sys/types.h>

#define COM_SYS_SIGNAL_SIGMASK_INIT(maskptr) *(maskptr) = 0UL;
#define COM_SYS_SIGNAL_SIGMASK_SET(maskptr, sig) \
    __atomic_fetch_or(maskptr, 1UL << ((sig) - 1), __ATOMIC_SEQ_CST)
#define COM_SYS_SIGNAL_SIGMASK_UNSET(maskptr, sig) \
    __atomic_fetch_and(maskptr, ~(1UL << ((sig) - 1)), __ATOMIC_SEQ_CST)
#define COM_SYS_SIGNAL_SIGMASK_ISSET(maskptr, sig) \
    __atomic_fetch_and(maskptr, 1UL << ((sig) - 1), __ATOMIC_SEQ_CST)
#define COM_SYS_SIGNAL_SIGMASK_GET(maskptr) \
    __atomic_load_n(maskptr, __ATOMIC_SEQ_CST)

typedef struct {
    unsigned long sig[1024 / (8 * sizeof(long))];
} com_sigset_t;

// Sigmask is essentially an entry into the sig array above
// The struct above is kept for compatibility with Linux (and mlibc currently),
// but there's no need for all those signals.
// So, to save some space in proc and thread structs, we use sigmask and only
// ever access sigset_t.sig[0]
typedef unsigned long com_sigmask_t;

#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/thread.h>

void com_sys_signal_sigset_emptY(com_sigset_t *set);
int  com_sys_signal_send_to_proc(pid_t pid, int sig, struct com_proc *sender);
int  com_sys_signal_send_to_proc_group(pid_t            pgid,
                                       int              sig,
                                       struct com_proc *sender);
int  com_sys_signal_send_to_thread(struct com_thread *thread,
                                   int                sig,
                                   struct com_proc   *sender);
void com_sys_signal_dispatch(arch_context_t *ctx);
