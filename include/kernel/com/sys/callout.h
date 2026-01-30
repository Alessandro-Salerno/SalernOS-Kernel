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

#include <kernel/com/sys/interrupt.h>
#include <lib/spinlock.h>
#include <stdbool.h>
#include <stdint.h>
#include <vendor/tailq.h>

typedef struct com_callout com_callout_t;
typedef void (*com_callout_intf_t)(com_callout_t *callout);

typedef struct com_callout {
    uintmax_t          ns;
    com_callout_intf_t handler;
    void              *arg;
    bool               reuse;
    TAILQ_ENTRY(com_callout) queue;
} com_callout_t;

TAILQ_HEAD(com_callout_tailq, com_callout);

typedef struct com_callout_queue {
    uintmax_t                ns;
    uintmax_t                next_preempt;
    struct com_callout_tailq queue;
    kspinlock_t              lock;
} com_callout_queue_t;

uintmax_t com_sys_callout_get_time(void);
void      com_sys_callout_run(void);
void      com_sys_callout_isr(com_isr_t *isr, arch_context_t *ctx);
void      com_sys_callout_reschedule_at(com_callout_t *callout, uintmax_t ns);
void      com_sys_callout_reschedule(com_callout_t *callout, uintmax_t delay);
void      com_sys_callout_add_at(com_callout_intf_t handler,
                                 void              *arg,
                                 uintmax_t          ns);
void      com_sys_callout_add(com_callout_intf_t handler,
                              void              *arg,
                              uintmax_t          delay);
void      com_sys_callout_add_at_bsp(com_callout_intf_t handler,
                                     void              *arg,
                                     uintmax_t          ns);
void      com_sys_callout_add_bsp(com_callout_intf_t handler,
                                  void              *arg,
                                  uintmax_t          delay);
void      com_sys_callout_set_bsp_nolock(com_callout_queue_t *bsp_queue);
