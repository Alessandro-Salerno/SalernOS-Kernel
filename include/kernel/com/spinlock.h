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
#include <kernel/com/log.h>
#include <stdbool.h>

typedef int com_spinlock_t;

#define COM_SPINLOCK_NEW() 0

static inline void hdr_com_spinlock_acquire(com_spinlock_t *lock) {
  hdr_arch_cpu_interrupt_disable();
  hdr_arch_cpu_get()->lock_depth++;
  while (!__sync_bool_compare_and_swap(lock, 0, 1)) {
    hdr_arch_cpu_pause();
  }
}

static inline bool hdr_com_spinlock_try(com_spinlock_t *lock) {
  return __sync_bool_compare_and_swap(lock, 0, 1);
}

static inline void hdr_com_spinlock_release(com_spinlock_t *lock) {
  ASSERT(0 < hdr_arch_cpu_get()->lock_depth);
  *lock = 0;
  hdr_arch_cpu_get()->lock_depth--;

  if (0 == hdr_arch_cpu_get()->lock_depth && hdr_arch_cpu_get()->intstatus) {
    hdr_arch_cpu_interrupt_enable();
  }
}
