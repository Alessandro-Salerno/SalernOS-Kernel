/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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

#include <arch/cpu.h>
#include <stdbool.h>

typedef int spinlock_t;

#define SPINLOCK_NEW() 0

static inline void hdr_com_spinlock_acquire(spinlock_t *lock) {
  while (!__sync_bool_compare_and_swap(lock, 0, 1))
    hdr_arch_cpu_pause();
}

static inline bool hdr_com_spinlock_try(spinlock_t *lock) {
  return __sync_bool_compare_and_swap(lock, 0, 1);
}

static inline void hdr_com_spinlock_release(spinlock_t *lock) {
  *lock = 0;
}
