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
#include <lib/sync.h>

void ksync_acquire(ksync_t *sync) {
    if (KSYNC_FLAGS_MUTEX & sync->flags) {
        kmutex_acquire(&sync->lock.mutex);
    } else {
        kspinlock_acquire(&sync->lock.spinlock);
    }
}

void ksync_release(ksync_t *sync) {
    if (KSYNC_FLAGS_MUTEX & sync->flags) {
        kmutex_release(&sync->lock.mutex);
    } else {
        kspinlock_release(&sync->lock.spinlock);
    }
}

void ksync_wait(ksync_t *sync, com_waitlist_t *waitlist) {
    if (KSYNC_FLAGS_MUTEX & sync->flags) {
        kmutex_wait(&sync->lock.mutex, waitlist, 0, true);
    } else {
        kspinlock_wait(&sync->lock.spinlock, waitlist, 0, true);
    }
}

int ksync_wait_timeout(ksync_t             *sync,
                       struct com_waitlist *waitlist,
                       uintmax_t            timeout) {
    if (KSYNC_FLAGS_MUTEX & sync->flags) {
        return kmutex_wait(&sync->lock.mutex, waitlist, timeout, true);
    }
    return kspinlock_wait(&sync->lock.spinlock, waitlist, timeout, true);
}

void ksync_notify(ksync_t *sync, com_waitlist_t *waitlist) {
    (void)sync;
    com_sys_sched_notify(waitlist);
}

void ksync_notify_all(ksync_t *sync, com_waitlist_t *waitlist) {
    (void)sync;
    com_sys_sched_notify_all(waitlist);
}

void ksync_wait_uninterruptible(ksync_t *sync, com_waitlist_t *waitlist) {
    if (KSYNC_FLAGS_MUTEX & sync->flags) {
        kmutex_wait(&sync->lock.mutex, waitlist, 0, false);
    } else {
        kspinlock_wait(&sync->lock.spinlock, waitlist, 0, false);
    }
}
