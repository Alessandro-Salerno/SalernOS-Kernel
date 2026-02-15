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
#include <kernel/com/io/log.h>
#include <kernel/com/sys/panic.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/thread.h>
#include <lib/spinlock.h>

#define INCREMENT_LOCK_DEPTH(thr) \
    if (NULL != thr)              \
        thr->lock_depth++;
#define DECREMENT_LOCK_DEPTH(thr) \
    if (NULL != thr)              \
        thr->lock_depth--;

#define INCREMENT_CURR_LOCK_DEPTH() INCREMENT_LOCK_DEPTH(ARCH_CPU_GET_THREAD())
#define DECREMENT_CURR_LOCK_DEPTH() DECREMENT_LOCK_DEPTH(ARCH_CPU_GET_THREAD())

#define LOCK_ERROR(fmt, ...)                             \
    com_sys_panic(NULL,                                  \
                  "(lock error) " fmt " [called at %p]", \
                  __VA_ARGS__,                           \
                  __builtin_return_address(0));

static inline void decrement_lock_depth_tested(void) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();

    if (NULL != curr_thread) {
        int old_depth = curr_thread->lock_depth--;

        if (old_depth < 1) {
            LOCK_ERROR("trying to unlock lock on thread (tid = %d) with "
                       "invalid lock depth %d",
                       curr_thread->tid,
                       old_depth);
        }

        if (1 == old_depth) {
            ARCH_CPU_ENABLE_INTERRUPTS();
        }
    }
}

void kspinlock_acquire(kspinlock_t *lock) {
    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_KSPINLOCK_ACQUIRE);
    ARCH_CPU_DISABLE_INTERRUPTS();
    INCREMENT_CURR_LOCK_DEPTH();

    while (true) {
        // this is before the spinning since hopefully the lock is uncontended
        if (KSPINLOCK_FREE_VALUE == __atomic_exchange_n(&lock->lock,
                                                        KSPINLOCK_HELD_VALUE,
                                                        __ATOMIC_ACQUIRE)) {
            break;
        }
        // spin with no ordering constraints
        while (KSPINLOCK_HELD_VALUE ==
               __atomic_load_n(&lock->lock, __ATOMIC_RELAXED)) {
            ARCH_CPU_PAUSE();
        }
    }

    lock->holder_thread   = ARCH_CPU_GET_THREAD();
    lock->last_acquire_ip = (void *)__builtin_return_address(0);
    lock->last_release_ip = NULL;
    com_sys_profiler_end_function(&profiler_data);
}

bool kspinlock_acquire_timeout(kspinlock_t *lock, uintmax_t timeout_ns) {
    ARCH_CPU_DISABLE_INTERRUPTS();
    INCREMENT_CURR_LOCK_DEPTH();

    uintmax_t start_ns = ARCH_CPU_GET_TIME();
    uintmax_t end_ns   = start_ns + timeout_ns;

    while (true) {
        // this is before the spinning since hopefully the lock is uncontended
        if (KSPINLOCK_FREE_VALUE == __atomic_exchange_n(&lock->lock,
                                                        KSPINLOCK_HELD_VALUE,
                                                        __ATOMIC_ACQUIRE)) {
            break;
        }
        // spin with no ordering constraints
        while (KSPINLOCK_HELD_VALUE ==
               __atomic_load_n(&lock->lock, __ATOMIC_RELAXED)) {
            if (end_ns <= ARCH_CPU_GET_TIME()) {
                goto fail;
            }
            ARCH_CPU_PAUSE();
        }
    }

    lock->holder_thread   = ARCH_CPU_GET_THREAD();
    lock->last_acquire_ip = (void *)__builtin_return_address(0);
    lock->last_release_ip = NULL;
    return true;

fail:
    // Control reaches here only in case of timeout
    DECREMENT_CURR_LOCK_DEPTH();
    return false;
}

void kspinlock_release(kspinlock_t *lock) {
    if (KSPINLOCK_FREE_VALUE == lock->lock) {
        LOCK_ERROR("trying to unlock unlocked lock %p", lock);
    }

    if (KSPINLOCK_HELD_VALUE != lock->lock) {
        LOCK_ERROR("lock at %p has impossible value %d", lock, lock->lock);
    }

    lock->last_release_ip = (void *)__builtin_return_address(0);
    lock->holder_thread   = NULL;
    lock->last_acquire_ip = NULL;
    __atomic_store_n(&lock->lock, KSPINLOCK_FREE_VALUE, __ATOMIC_RELEASE);
    decrement_lock_depth_tested();
}

void kspinlock_fake_acquire(void) {
    ARCH_CPU_DISABLE_INTERRUPTS();
    INCREMENT_CURR_LOCK_DEPTH();
}

void kspinlock_fake_release(void) {
    decrement_lock_depth_tested();
}
