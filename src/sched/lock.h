/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2023 Alessandro Salerno

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**********************************************************************/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCHED_LOCK_NEW()             \
  (lock_t) {                         \
    ._Lock = 0, ._LastAquirer = NULL \
  }

typedef struct Lock {
  uint32_t _Lock;
  void    *_LastAquirer;
} lock_t;

static inline bool sched_lock_test_acquire(lock_t *__lock) {
  return __sync_bool_compare_and_swap(&__lock->_Lock, 0, 1);
}

static inline void sched_lock_release(lock_t *__lock) {
  __lock->_LastAquirer = NULL;
  __atomic_store_n(&__lock->_Lock, 0, __ATOMIC_SEQ_CST);
}

void sched_lock_acquire(lock_t *__lock);
void sched_lock_acquire_unchecked(lock_t *__lock);
