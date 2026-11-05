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

#pragma once

#include <lib/spinlock.h>
#include <stddef.h>
#include <vendor/tailq.h>

struct com_thread;
TAILQ_HEAD(com_thread_tailq, com_thread);
SLIST_HEAD(com_thread_slist, com_thread);

#define COM_SYS_THREAD_WAITLIST_INIT(wl_ptr) \
    TAILQ_INIT(&(wl_ptr)->queue);            \
    (wl_ptr)->count = 0;                     \
    (wl_ptr)->lock  = KSPINLOCK_NEW()

#define COM_SYS_THREAD_WAITLIST_EMPTY(wl_ptr) TAILQ_EMPTY(&(wl_ptr)->queue)

typedef struct com_waitlist {
    struct com_thread_tailq queue;
    kspinlock_t             lock;
    size_t                  count;
} com_waitlist_t;

typedef enum com_thread_state {
    E_COM_THREAD_STATE_NEW = 0,
    E_COM_THREAD_STATE_WAITING,
    E_COM_THREAD_STATE_READY,
    E_COM_THREAD_STATE_RUNNING,
    E_COM_THREAD_STATE_EXITED
} com_thread_state_t;

#include <arch/context.h>
#include <arch/cpu.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/proc.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/types.h>

typedef struct com_thread_timer {
    kspinlock_t      lock;
    struct itimerval itimerval;
    uintmax_t        ctime;      // when it was created
    void            *itimer_arg; // see src/com/sys/syscall/setitimer.c
} com_thread_timer_t;

typedef struct com_thread {
    void                *kernel_stack;
    arch_context_t       ctx;
    arch_context_extra_t xctx;
    kspinlock_t         *sched_lock;
    uintptr_t            epoch;
    size_t               refs;
    SLIST_ENTRY(com_thread) zombies;
    bool               exit_requested;
    bool               signal_stopped; // protected by proc->signal_lock
    bool               wait_interruptible;
    struct com_proc   *proc;
    struct arch_cpu   *cpu;
    struct arch_cpu   *last_cpu;
    com_thread_state_t state; // relaxed atomic accesses
    TAILQ_ENTRY(com_thread) threads;
    TAILQ_ENTRY(com_thread) proc_threads;
    int   lock_depth;
    pid_t tid;

    arch_context_t *usercopy_ctx;

    com_waitlist_t     *waiting_on;
    uintmax_t           ctime;
    uintmax_t           time_slept;
    struct com_callout *timed_wait_callout;
    int                 wait_error;

    com_sigmask_t pending_signals;
    com_sigmask_t masked_signals;

    // Each outstanding interval-timer callout owns a thread reference
    com_thread_timer_t real_timer;
} com_thread_t;

com_thread_t *com_sys_thread_new(struct com_proc *proc,
                                 void            *stack,
                                 uintmax_t        stack_size,
                                 void            *entry);
com_thread_t *
com_sys_thread_new_kernel(struct com_proc *proc, void *entry, void *arg);

void com_sys_thread_lock(com_thread_t *thread);
void com_sys_thread_unlock(com_thread_t *thread);
void com_sys_thread_hold(com_thread_t *thread);
void com_sys_thread_release(com_thread_t *thread);
void com_sys_thread_reap(void);

void com_sys_thread_transition(com_thread_t *thread, com_thread_state_t state);
void com_sys_thread_transition_nolock(com_thread_t      *thread,
                                      com_thread_state_t state);
void com_sys_thread_request_exit(com_thread_t *thread);
void com_sys_thread_exit(com_thread_t *thread);
void com_sys_thread_exit_nolock(com_thread_t *thread);
// Release a caller-owned reference after switch-out and final freeing is
// deferred
void com_sys_thread_destroy(com_thread_t *thread);
void com_sys_thread_ready_nolock(com_thread_t *thread);
void com_sys_thread_ready(com_thread_t *thread);
