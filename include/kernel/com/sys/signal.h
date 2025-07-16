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
#include <signal.h>
#include <stdint.h>
#include <sys/types.h>

// NOTE: These must be used in a thread-safe manner, i.e. while holding the
// process'es signal_lock, since they're no longer atomic
#define COM_SYS_SIGNAL_SIGMASK_INIT(maskptr) *(maskptr) = 0UL;
#define COM_SYS_SIGNAL_SIGMASK_SET(maskptr, sig) \
    (*(maskptr) |= 1UL << ((sig) - 1))
#define COM_SYS_SIGNAL_SIGMASK_UNSET(maskptr, sig) \
    (*(maskptr) &= ~(1UL << ((sig) - 1)))
#define COM_SYS_SIGNAL_SIGMASK_ISSET(maskptr, sig) \
    (*(maskptr) & (1UL << ((sig) - 1)))

typedef struct {
    unsigned long sig[1024 / (8 * sizeof(long))];
} com_sigset_t;

// Sigmask is essentially an entry into the sig array above
// The struct above is kept for compatibility with Linux (and mlibc currently),
// but there's no need for all those signals.
// So, to save some space in proc and thread structs, we use sigmask and only
// ever access sigset_t.sig[0]
typedef unsigned long com_sigmask_t;

typedef struct com_sigaction {
    void (*sa_action)(int);
    // void (*sa_action)(int, siginfo_t *, void *);
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    com_sigset_t sa_mask;
} com_sigaction_t;

typedef struct com_ucontext {
    unsigned long        uc_flags;
    struct com_ucontext *uc_link;
    stack_t              uc_stack;
    mcontext_t           uc_mcontext;
    com_sigset_t         uc_sigmask;
} com_ucontext_t;

typedef struct com_sigframe {
    siginfo_t      info;
    com_ucontext_t uc;
} com_sigframe_t;

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
void com_sys_signal_dispatch(arch_context_t *ctx, struct com_thread *thread);
int  com_sys_signal_set_mask(com_sigmask_t  *mask,
                             int             how,
                             com_sigset_t   *set,
                             com_sigset_t   *oset,
                             com_spinlock_t *signal_lock);
int  com_sys_signal_check_nolock(void);
int  com_sys_signal_check(void);
