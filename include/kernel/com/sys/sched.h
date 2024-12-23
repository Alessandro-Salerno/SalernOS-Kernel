/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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

void com_sys_sched_yield(void);
void com_sys_sched_isr(com_isr_t *isr, arch_context_t *ctx);

// TODO: com_thread_tailq cannot be used because of circular dependencies
void com_sys_sched_wait(void *waiting_on, void *cond);
void com_sys_sched_notify(void *waiters);

void com_sys_sched_init(void);
