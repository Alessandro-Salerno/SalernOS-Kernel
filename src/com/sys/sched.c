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

#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
#include <vendor/tailq.h>

void com_sys_sched_run(arch_context_t *ctx) {
  arch_cpu_t   *cpu  = hdr_arch_cpu_get();
  com_thread_t *curr = cpu->thread;
  com_thread_t *next = TAILQ_FIRST(&cpu->sched_queue);

  if (NULL == curr) {
    return;
  }

  if (NULL == next) {
    if (curr->runnable) {
      return;
    }

    // TODO: idle thread
  } else {
    TAILQ_REMOVE_HEAD(&cpu->sched_queue, threads);
  }

  // TODO: add idle thread check
  if (curr->runnable) {
    TAILQ_INSERT_TAIL(&cpu->sched_queue, curr, threads);
  }

  arch_mmu_pagetable_t *prev_pt = NULL;
  arch_mmu_pagetable_t *next_pt = NULL;

  if (NULL != curr->proc) {
    prev_pt = curr->proc->page_table;
  }

  if (NULL != next->proc) {
    next_pt = next->proc->page_table;
  }

  if (NULL != next_pt && next_pt != prev_pt) {
    arch_mmu_switch(next_pt);
  }

  curr->cpu = NULL;
  next->cpu = cpu;

  // Save the previous threads' context
  // ARCH_CONTEXT_COPY(&curr->ctx, ctx);

  // Restore the next thread's context
  // ARCH_CONTEXT_COPY(ctx, &next->ctx);

  ARCH_CPU_SET_KERNEL_STACK(cpu, next->kernel_stack);
  cpu->thread = next;
  ARCH_CONTEXT_SWITCH(&next->ctx, &curr->ctx);
}
