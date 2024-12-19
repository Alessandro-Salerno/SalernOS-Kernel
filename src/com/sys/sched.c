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

void com_sys_sched_yield() {
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

  // Restore kernel stack
  ARCH_CPU_SET_KERNEL_STACK(cpu, next->kernel_stack);

  // Switch threads
  // NOTE: this doesn't jump to userspace directly! Instead, this switches the
  //        stack pointer and other registers so that when returning from the
  //        underlying assembly function, control is handed to whatever address
  //        is on the stack, which is most likely this function. Once the
  //        execution gets back here, com_sys_sched_yield cna return to its
  //        caller, which can be a hardware timer or anything else (e.g., wait).
  //        Remember that, since the stack has changed, there's no guarantee
  //        that this function will return to the same point from which it was
  //        called, in fact, quite the opposite.
  cpu->thread = next;
  ARCH_CONTEXT_SWITCH(&next->ctx, &curr->ctx);
}

void com_sys_sched_isr(com_isr_t *isr, arch_context_t *ctx) {
  (void)isr;
  (void)ctx;
  com_sys_sched_yield();
}
