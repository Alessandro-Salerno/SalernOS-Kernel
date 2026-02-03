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
#include <lib/mutex.h>
#include <lib/spinlock.h>

#define KSYNC_FLAGS_MUTEX 1

#define KSYNC_INIT_BASE(so_ptr, f) (so_ptr)->flags = f

#define KSYNC_INIT_MUTEX(so_ptr)                   \
    KSYNC_INIT_BASE(so_ptr, KSYNC_FLAGS_MUTEX); \
    KMUTEX_INIT(&(so_ptr)->lock.mutex)

#define KSYNC_INIT_SPINLOCK(so_ptr) \
    KSYNC_INIT_BASE(so_ptr, 0);     \
    (so_ptr)->lock.spinlock = KSPINLOCK_NEW()

#define KSYNC_NEW_SPINLOCK() \
    (ksync_t){.lock.spinlock = KSPINLOCK_NEW(), .flags = 0}

typedef struct ksync {
    union {
        kmutex_t    mutex;
        kspinlock_t spinlock;
    } lock;
    int flags;
} ksync_t;

void ksync_acquire(ksync_t *sync);
void ksync_release(ksync_t *sync);
void ksync_wait(ksync_t *sync, struct com_waitlist *waitlist);
void ksync_notify(ksync_t *sync, struct com_waitlist *waiters);
void ksync_notify_all(ksync_t *sync, struct com_waitlist *waiters);
