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

#include <kernel/com/sys/thread.h>
#include <lib/spinlock.h>
#include <stdbool.h>
#include <vendor/tailq.h>

#define KWORK_INIT(workptr, h) \
    (workptr)->handler = h;    \
    (workptr)->pending = false

typedef struct kwork {
    void (*handler)(struct kwork *work);
    TAILQ_ENTRY(kwork) queue;
    bool pending;
} kwork_t;

typedef struct kworkqueue {
    kspinlock_t lock;
    TAILQ_HEAD(, kwork) works;
    com_waitlist_t     waitlist;
    struct com_thread *worker;
} kworkqueue_t;

void kworkqueue_init(kworkqueue_t *wq);
void kworkqueue_push(kworkqueue_t *wq, kwork_t *work);
