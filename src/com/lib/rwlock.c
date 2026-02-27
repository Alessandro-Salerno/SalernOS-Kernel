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

#include <kernel/com/sys/sched.h>
#include <lib/rwlock.h>

// NOTE: all of this assumes FIFO waitlists. Keep this in mind when we implement
// more advanced scheduling systems: there will be a need for FIFO waitlists

void krwlock_acquire_read(krwlock_t *rwlock) {
    kspinlock_acquire(&rwlock->lock);

    // Readers use the current read_phase_ticket to gate entry
    while (rwlock->writer_active ||
           rwlock->next_ticket - 1 > rwlock->read_phase_ticket) {
        com_sys_sched_wait(&rwlock->readers_waitlist, &rwlock->lock);
    }

    rwlock->active_readers++;
    kspinlock_release(&rwlock->lock);
}

void krwlock_release_read(krwlock_t *rwlock) {
    kspinlock_acquire(&rwlock->lock);

    rwlock->active_readers--;
    if (0 == rwlock->active_readers) {
        // If a writer is waiting, wake exactly one
        if (rwlock->serving_ticket != rwlock->next_ticket) {
            com_sys_sched_notify(&rwlock->writers_waitlist);
        }
    }

    kspinlock_release(&rwlock->lock);
}

void krwlock_acquire_write(krwlock_t *rwlock) {
    kspinlock_acquire(&rwlock->lock);
    uintmax_t ticket = rwlock->next_ticket++;

    while (rwlock->writer_active || rwlock->active_readers > 0 ||
           ticket != rwlock->serving_ticket) {
        com_sys_sched_wait(&rwlock->writers_waitlist, &rwlock->lock);
    }

    KASSERT(!rwlock->writer_active);
    rwlock->writer_active = true;
    kspinlock_release(&rwlock->lock);
}

void krwlock_release_write(krwlock_t *rwlock) {
    kspinlock_acquire(&rwlock->lock);
    rwlock->writer_active     = false;
    rwlock->read_phase_ticket = rwlock->next_ticket - 1;
    rwlock->serving_ticket++;

    if (rwlock->serving_ticket != rwlock->next_ticket) {
        // More writers waiting: wake exactly one
        com_sys_sched_notify(&rwlock->writers_waitlist);
    } else {
        // No writers waiting: wake all readers in current phase
        com_sys_sched_notify_all(&rwlock->readers_waitlist);
    }

    kspinlock_release(&rwlock->lock);
}
