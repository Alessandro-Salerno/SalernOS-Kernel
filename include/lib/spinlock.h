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

#include <stdbool.h>
#include <stdint.h>

#define KSPINLOCK_HELD_VALUE 144
#define KSPINLOCK_FREE_VALUE 0

#define KSPINLOCK_NEW()                                  \
    (kspinlock_t){.lock          = KSPINLOCK_FREE_VALUE, \
                  .holder_thread = (void *)0xAAAAAAA}
#define KSPINLOCK_IS_HELD(lockptr) (KSPINLOCK_HELD_VALUE == (lockptr)->lock)

typedef struct kspinlock {
    int   lock;
    void *holder_thread;
    void *last_acquire_ip;
    void *last_release_ip;
} kspinlock_t;

void kspinlock_acquire(kspinlock_t *lock);
bool kspinlock_acquire_timeout(kspinlock_t *lock, uintmax_t timeout_ns);
void kspinlock_release(kspinlock_t *lock);
void kspinlock_fake_acquire(void);
void kspinlock_fake_release(void);
