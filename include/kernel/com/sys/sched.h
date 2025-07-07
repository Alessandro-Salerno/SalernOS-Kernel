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
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/thread.h>

void com_sys_sched_yield(void);
void com_sys_sched_isr(com_isr_t *isr, arch_context_t *ctx);

void com_sys_sched_wait(struct com_thread_tailq *waiting_on,
                        com_spinlock_t          *cond);
void com_sys_sched_notify(struct com_thread_tailq *waiters);
void com_sys_sched_notify_all(struct com_thread_tailq *waiters);

void com_sys_sched_init(void);
