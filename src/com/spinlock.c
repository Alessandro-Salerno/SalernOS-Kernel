/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
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
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/thread.h>

void com_spinlock_acquire(com_spinlock_t *lock) {
    hdr_arch_cpu_interrupt_disable();
    com_thread_t *curr_thread = hdr_arch_cpu_get_thread();
    if (NULL != curr_thread) {
        curr_thread->lock_depth++;
    }
    while (true) {
        // this is before the spinning since hopefully the lock is uncontended
        if (!__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE)) {
            return;
        }
        // spin with no ordering constraints
        while (__atomic_load_n(lock, __ATOMIC_RELAXED)) {
            hdr_arch_cpu_pause();
        }
    }
}

void com_spinlock_release(com_spinlock_t *lock) {
    if (!(*lock)) {
        KDEBUG("trying to unlock unlocked lock at %x",
               __builtin_return_address(0));
    }
    KASSERT(1 == *lock);
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
    com_thread_t *curr_thread = hdr_arch_cpu_get_thread();
    if (NULL != curr_thread) {
        int oldval = curr_thread->lock_depth--;
        if (oldval == 0) {
            KDEBUG("trying to unlock with depth 0 at %x",
                   __builtin_return_address(0));
        }
        KASSERT(oldval != 0);
        if (oldval == 1) {
            hdr_arch_cpu_interrupt_enable();
        }
    }
}

void com_spinlock_fake_release() {
    com_thread_t *curr_thread = hdr_arch_cpu_get_thread();
    if (NULL != curr_thread) {
        int oldval = curr_thread->lock_depth--;
        KASSERT(oldval != 0);
        if (oldval == 1) {
            hdr_arch_cpu_interrupt_enable();
        }
    }
}
