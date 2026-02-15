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
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/sched.h>
#include <lib/mutex.h>
#include <lib/util.h>

// CREDIT: vloxei64/ke
// TODO: why doesn't this work at boot time?

static bool try_acquire_internal(kmutex_t *mutex, size_t retries) {
    for (size_t i = 0; i < retries; i++) {
        kspinlock_acquire(&mutex->lock);
        if (!mutex->locked) {
            return true;
        }
        // TODO: check if owner is running on the same CPU and go straight to
        // wait. This cannot be odne right now because, logically, if we are
        // running, no other thread is running on this CPU, so mutex->owner->cpu
        // will be NULL. We need a new field in the thread struct to hold last
        // CPU.
        kspinlock_release(&mutex->lock);
        ARCH_CPU_PAUSE();
    }

    return false;
}

bool kmutex_try_acquire(kmutex_t *mutex) {
    if (!try_acquire_internal(mutex, 1)) {
        return false;
    }

    mutex->locked = true;
    mutex->owner  = ARCH_CPU_GET_THREAD();
    kspinlock_release(&mutex->lock);
    return true;
}

void kmutex_acquire(kmutex_t *mutex) {
    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_KMUTEX_ACQUIRE);
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    KASSERT(NULL == curr_thread || 0 == curr_thread->lock_depth);

    if (try_acquire_internal(mutex, CONFIG_MUTEX_RETRIES)) {
        goto take;
    }

    kspinlock_acquire(&mutex->lock);
    while (mutex->locked) {
        com_sys_sched_wait(&mutex->waiters, &mutex->lock);
    }

take:
    mutex->locked = true;
    mutex->owner  = curr_thread;
    kspinlock_release(&mutex->lock);
    com_sys_profiler_end_function(&profiler_data);
}

void kmutex_release(kmutex_t *mutex) {
    // com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    // KASSERT(NULL == curr_thread || 0 == curr_thread->lock_depth);

    kspinlock_acquire(&mutex->lock);
    KASSERT(mutex->locked);
    mutex->locked = false;
    mutex->owner  = NULL;
    com_sys_sched_notify(&mutex->waiters);
    kspinlock_release(&mutex->lock);
    // com_sys_sched_yield();
}
