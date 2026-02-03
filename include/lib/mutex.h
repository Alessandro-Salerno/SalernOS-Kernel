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
#include <stddef.h>
#include <vendor/tailq.h>

#define KMUTEX_INIT(mutexptr)             \
    (mutexptr)->lock   = KSPINLOCK_NEW(); \
    (mutexptr)->owner  = NULL;            \
    (mutexptr)->locked = false;           \
    COM_SYS_THREAD_WAITLIST_INIT(&(mutexptr)->waiters)

typedef struct kmutex {
    kspinlock_t        lock;
    bool               locked;
    struct com_thread *owner;
    com_waitlist_t     waiters;
} kmutex_t;

void kmutex_acquire(kmutex_t *mutex);
void kmutex_release(kmutex_t *mutex);
