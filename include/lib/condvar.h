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

#define KCONDVAR_FLAGS_MUTEX 1

#define KCONDVAR_INIT_BASE(cv, f) (cv)->flags = f

#define KCONDVAR_INIT_MUTEX(cv)                   \
    KCONDVAR_INIT_BASE(cv, KCONDVAR_FLAGS_MUTEX); \
    KMUTEX_INIT(&(cv)->lock.mutex)

#define KCONDVAR_INIT_SPINLOCK(cv) \
    KCONDVAR_INIT_BASE(cv, 0);     \
    (cv)->lock.spinlock = KSPINLOCK_NEW()

typedef struct kcondvar {
    union {
        kmutex_t    mutex;
        kspinlock_t spinlock;
    } lock;
    int flags;
} kcondvar_t;

void kcondvar_acquire(kcondvar_t *condvar);
void kcondvar_release(kcondvar_t *condvar);
void kcondvar_wait(kcondvar_t *condvar, struct com_thread_tailq *waitlist);
void kcondvar_notifY(kcondvar_t *condvar, struct com_thread_tailq *waiters);
void kcondvar_notify_all(kcondvar_t *condvar, struct com_thread_tailq *waiters);
