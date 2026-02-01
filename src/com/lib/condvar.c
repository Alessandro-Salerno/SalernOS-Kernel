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
#include <lib/condvar.h>

void kcondvar_acquire(kcondvar_t *condvar) {
    if (KCONDVAR_FLAGS_MUTEX & condvar->flags) {
        kmutex_acquire(&condvar->lock.mutex);
    } else {
        kspinlock_acquire(&condvar->lock.spinlock);
    }
}

void kcondvar_release(kcondvar_t *condvar) {
    if (KCONDVAR_FLAGS_MUTEX & condvar->flags) {
        kmutex_release(&condvar->lock.mutex);
    } else {
        kspinlock_release(&condvar->lock.spinlock);
    }
}

void kcondvar_wait(kcondvar_t *condvar, struct com_thread_tailq *waitlist) {
    if (KCONDVAR_FLAGS_MUTEX & condvar->flags) {
        com_sys_sched_wait_mutex(waitlist, &condvar->lock.mutex);
    } else {
        com_sys_sched_wait(waitlist, &condvar->lock.spinlock);
    }
}

void kcondvar_notifY(kcondvar_t *condvar, struct com_thread_tailq *waitlist) {
    (void)condvar;
    com_sys_sched_notify(waitlist);
}

void kcondvar_notify_all(kcondvar_t              *condvar,
                         struct com_thread_tailq *waitlist) {
    (void)condvar;
    com_sys_sched_notify_all(waitlist);
}
