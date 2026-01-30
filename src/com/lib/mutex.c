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

#include <arch/cpu.h>
#include <kernel/com/sys/sched.h>
#include <lib/mutex.h>
#include <lib/util.h>

// CREDIT: vloxei64/ke
// TODO: why doesn't this work at boot time?

void kmutex_acquire(kmutex_t *mutex) {
#if CONFIG_MUTEX_MODE == CONST_MUTEX_SPINLOCK
    kspinlock_acquire(&mutex->lock);
#elif CONFIG_MUTEX_MODE == CONST_MUTEX_REAL
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    KASSERT(NULL == curr_thread || 0 == curr_thread->lock_depth);

    kspinlock_acquire(&mutex->lock);
    while (mutex->locked) {
        com_sys_sched_wait(&mutex->waiters, &mutex->lock);
    }

    mutex->locked = true;
    mutex->owner  = curr_thread;
    kspinlock_release(&mutex->lock);
#endif
}

void kmutex_release(kmutex_t *mutex) {
#if CONFIG_MUTEX_MODE == CONST_MUTEX_SPINLOCK
    kspinlock_release(&mutex->lock);
#elif CONFIG_MUTEX_MODE == CONST_MUTEX_REAL
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    KASSERT(NULL == curr_thread || 0 == curr_thread->lock_depth);

    kspinlock_acquire(&mutex->lock);
    KASSERT(mutex->locked);
    mutex->locked = false;
    mutex->owner  = NULL;
    com_sys_sched_notify(&mutex->waiters);
    kspinlock_release(&mutex->lock);
    com_sys_sched_yield();
#endif
}
