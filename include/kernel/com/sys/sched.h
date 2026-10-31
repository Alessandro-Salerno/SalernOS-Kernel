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

#include <arch/context.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/thread.h>
#include <lib/spinlock.h>

// On EINTR authority remains held and on success it is released on switch-in
int  com_sys_sched_yield_nolock(bool                   interruptible,
                                arch_context_message_t message);
void com_sys_sched_switch_finish(arch_context_message_t message);

void com_sys_sched_yield(void);
void com_sys_sched_isr(com_isr_t *isr, arch_context_t *ctx);

// prepare precedes authority and release follows wait publication under
// authority acquire runs outside authority and restores the primitive on every
// return The message callback receives success=true on switch-in, or false
// after failed-yield rollback and authority release, before reacquiring the
// primitive
typedef struct com_sched_wait_ops {
    void (*prepare)(void *primitive);
    arch_context_message_t (*release)(void *primitive);
    void (*acquire)(void *primitive);
} com_sched_wait_ops_t;

int  com_sys_sched_wait(com_waitlist_t             *waitlist,
                        uintmax_t                   timeout,
                        bool                        interruptible,
                        const com_sched_wait_ops_t *ops,
                        void                       *primitive);
void com_sys_sched_wait_nodrop(com_waitlist_t *waitlist);
int  com_sys_sched_wait_nodrop_timeout(com_waitlist_t *waitlist,
                                       uintmax_t       timeout);

void com_sys_sched_notify(com_waitlist_t *waitlist);
void com_sys_sched_notify_all(com_waitlist_t *waitlist);
void com_sys_sched_notify_n(com_waitlist_t *waitlist, size_t num_waiters);
void com_sys_sched_interrupt_nolock(com_thread_t *thread);
void com_sys_sched_notify_thread_nolock(com_thread_t *thread);
void com_sys_sched_notify_thread(com_thread_t *thread);

void com_sys_sched_prioritize(com_thread_t *thread);
void com_sys_sched_prioritize_nolock(com_thread_t *thread);

void com_sys_sched_init(void);
