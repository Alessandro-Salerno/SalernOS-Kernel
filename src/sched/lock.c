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

#include "sched/lock.h"

#include "kernelpanic.h"

#define MAX_ITERATIONS_BEFORE_DEADLOCK 100000000

__attribute__((noinline)) void sched_lock_acquire(lock_t *__lock) {
  volatile size_t _dead_count = 0;

  while (true) {
    if (sched_lock_test_acquire(__lock)) {
      break;
    }

    if (++_dead_count >= MAX_ITERATIONS_BEFORE_DEADLOCK) {
      panic_throw("Failed to acquire lock", NULL);
    }

    asm volatile("pause");
  }

  __lock->_LastAquirer = __builtin_return_address(0);
}

__attribute__((noinline)) void sched_lock_acquire_unchecked(lock_t *__lock) {
  while (true) {
    if (sched_lock_test_acquire(__lock)) {
      break;
    }

    asm volatile("pause");
  }

  __lock->_LastAquirer = __builtin_return_address(0);
}
