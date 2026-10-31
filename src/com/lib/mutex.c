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
#include <errno.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/sched.h>
#include <lib/mutex.h>
#include <lib/util.h>

static bool try_acquire_internal(kmutex_t *mutex, size_t retries) {
    for (size_t i = 0; i < retries; i++) {
        kspinlock_acquire(&mutex->lock);
        if (!mutex->locked) {
            return true;
        }

        if (__atomic_load_n(&mutex->owner->state, __ATOMIC_RELAXED) !=
            E_COM_THREAD_STATE_RUNNING) {
            kspinlock_release(&mutex->lock);
            return false;
        }

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
    if (NULL == ARCH_CPU_GET_THREAD()) {
        return;
    }

    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_KMUTEX_ACQUIRE);
    kmutex_acquire_timeout(mutex, 0);
    com_sys_profiler_end_function(&profiler_data);
}

bool kmutex_acquire_timeout(kmutex_t *mutex, uintmax_t timeout) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL == curr_thread) {
        return true;
    }
    KASSERT(NULL == curr_thread || 0 == curr_thread->lock_depth);

    if (try_acquire_internal(mutex, CONFIG_MUTEX_RETRIES)) {
        goto take;
    }

    kspinlock_acquire(&mutex->lock);
    while (mutex->locked) {
        if (ETIMEDOUT ==
            kspinlock_wait(&mutex->lock, &mutex->waiters, timeout, false)) {
            KASSERT(0 != timeout);
            kspinlock_release(&mutex->lock);
            com_sys_sched_notify(&mutex->waiters);
            return false;
        }
    }

take:
    mutex->locked = true;
    mutex->owner  = curr_thread;
    kspinlock_release(&mutex->lock);
    return true;
}

void kmutex_release(kmutex_t *mutex) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL == curr_thread) {
        return;
    }

    // KASSERT(NULL == curr_thread || 0 == curr_thread->lock_depth);

    kspinlock_acquire(&mutex->lock);
    KASSERT(mutex->locked);
    mutex->locked = false;
    mutex->owner  = NULL;
    com_sys_sched_notify(&mutex->waiters);
    kspinlock_release(&mutex->lock);
    // com_sys_sched_yield();
}

static void mutex_wait_prepare(void *primitive) {
    kmutex_t *mutex = primitive;
    kspinlock_acquire(&mutex->lock);
}

static void mutex_post_switch(void *arg, uintptr_t epoch, bool success) {
    com_thread_t *thread = arg;
    if (NULL == thread) {
        return;
    }
    if (success) {
        com_sys_thread_lock(thread);
        if (epoch == thread->epoch) {
            KASSERT(NULL != thread->waiting_on);
            com_sys_sched_notify_thread_nolock(thread);
        }
        com_sys_thread_unlock(thread);
    }
    com_sys_thread_release(thread);
}

static arch_context_message_t mutex_wait_release(void *primitive) {
    kmutex_t              *mutex   = primitive;
    arch_context_message_t message = {.callback = mutex_post_switch};
    KASSERT(mutex->locked && mutex->owner == ARCH_CPU_GET_THREAD());
    mutex->locked = false;
    mutex->owner  = NULL;
    kspinlock_acquire(&mutex->waiters.lock);
    com_thread_t *thread = TAILQ_FIRST(&mutex->waiters.queue);
    if (NULL != thread) {
        com_sys_thread_hold(thread);
        message.arg   = thread;
        message.value = __atomic_load_n(&thread->epoch, __ATOMIC_RELAXED);
    }
    kspinlock_release(&mutex->waiters.lock);
    kspinlock_release(&mutex->lock);
    return message;
}

static void mutex_wait_acquire(void *primitive) {
    kmutex_acquire(primitive);
}

int kmutex_wait(kmutex_t       *mutex,
                com_waitlist_t *waitlist,
                uintmax_t       timeout,
                bool            interruptible) {
    static const com_sched_wait_ops_t ops = {.prepare = mutex_wait_prepare,
                                             .release = mutex_wait_release,
                                             .acquire = mutex_wait_acquire};
    return com_sys_sched_wait(waitlist, timeout, interruptible, &ops, mutex);
}
