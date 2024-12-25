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

#include <vendor/tailq.h>
struct com_thread;
TAILQ_HEAD(com_thread_tailq, com_thread);

#include <arch/context.h>
#include <arch/cpu.h>
#include <kernel/com/sys/proc.h>
#include <stdbool.h>

typedef struct com_thread {
  arch_context_t   ctx;
  struct com_proc *proc;
  struct arch_cpu *cpu;
  bool             runnable;
  void            *kernel_stack;
  TAILQ_ENTRY(com_thread) threads;
  struct com_thread_tailq *waiting_on;
} com_thread_t;

com_thread_t *com_sys_thread_new(struct com_proc *proc,
                                 void            *stack,
                                 uintmax_t        stack_size,
                                 void            *entry);

void com_sys_thread_destroy(com_thread_t *thread);
void com_sys_thread_ready(com_thread_t *thread);
