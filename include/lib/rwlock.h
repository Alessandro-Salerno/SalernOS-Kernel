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

#define KRWLOCK_INIT(rwlock_ptr)                                       \
    do {                                                               \
        (rwlock_ptr)->lock              = KSPINLOCK_NEW();             \
        (rwlock_ptr)->next_ticket       = 1;                           \
        (rwlock_ptr)->serving_ticket    = 0;                           \
        (rwlock_ptr)->read_phase_ticket = 0;                           \
        (rwlock_ptr)->active_readers    = 0;                           \
        (rwlock_ptr)->writer_active     = false;                       \
        COM_SYS_THREAD_WAITLIST_INIT(&(rwlock_ptr)->readers_waitlist); \
        COM_SYS_THREAD_WAITLIST_INIT(&(rwlock_ptr)->writers_waitlist); \
    } while (0)

typedef struct krwlock {
    kspinlock_t lock;
    uintmax_t   next_ticket;       // next ticket to hand out (writers only)
    uintmax_t   serving_ticket;    // current ticket being served
    uintmax_t   read_phase_ticket; // max ticket readers can enter under
    size_t      active_readers;
    bool        writer_active;

    com_waitlist_t readers_waitlist;
    com_waitlist_t writers_waitlist;
} krwlock_t;

void krwlock_acquire_read(krwlock_t *rwlock);
void krwlock_release_read(krwlock_t *rwlock);
void krwlock_acquire_write(krwlock_t *rwlock);
void krwlock_release_write(krwlock_t *rwlock);
