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

#include <vendor/tailq.h>
struct com_thread;
TAILQ_HEAD(com_thread_tailq, com_thread);

#include <arch/context.h>
#include <arch/cpu.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/proc.h>
#include <lib/spinlock.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/types.h>

typedef struct com_thread_timer {
    struct itimerval itimerval;
    uintmax_t        ctime; // when it was created
} com_thread_timer_t;

typedef struct com_thread {
    void                *kernel_stack;
    arch_context_t       ctx;
    arch_context_extra_t xctx;
    kspinlock_t          sched_lock;
    struct com_proc     *proc;
    struct arch_cpu     *cpu;
    bool                 runnable;
    bool                 exited;
    TAILQ_ENTRY(com_thread) threads;
    TAILQ_ENTRY(com_thread) proc_threads;
    int   lock_depth;
    pid_t tid;

    struct com_thread_tailq *waiting_on;
    kspinlock_t             *waiting_cond;

    com_sigmask_t pending_signals;
    com_sigmask_t masked_signals;

    // TODO: whas if a thread exits while waiting for a timer? Callout will do
    // its things and all of a sudden we got a page fault in kernel or memory
    // corruption
    com_thread_timer_t real_timer;
} com_thread_t;

com_thread_t *com_sys_thread_new(struct com_proc *proc,
                                 void            *stack,
                                 uintmax_t        stack_size,
                                 void            *entry);
com_thread_t *com_sys_thread_new_kernel(struct com_proc *proc, void *entry);
void          com_sys_thread_exit(com_thread_t *thread);
void          com_sys_thread_exit_nolock(com_thread_t *thread);
void          com_sys_thread_destroy(com_thread_t *thread);
void          com_sys_thread_ready_nolock(com_thread_t *thread);
void          com_sys_thread_ready(com_thread_t *thread);
