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

#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/x86-64/arch/mmu.h>
#include <kernel/platform/x86-64/ist.h>
#include <kernel/platform/x86-64/msr.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct arch_cpu {
  struct com_thread    *thread;
  struct arch_cpu      *self;
  void                 *dummy2;
  uint64_t              id;
  uint64_t              gdt[7];
  x86_64_ist_t          ist;
  bool                  intstatus;
  com_isr_t             isr[256];
  arch_mmu_pagetable_t *root_page_table;
  uint64_t              lock_depth;
} arch_cpu_t;

static inline void hdr_arch_cpu_set(arch_cpu_t *cpu) {
  cpu->self = cpu;
  hdr_x86_64_msr_write(X86_64_MSR_GSBASE, (uint64_t)cpu);
}

static inline arch_cpu_t *hdr_arch_cpu_get() {
  arch_cpu_t *cpu;
  asm volatile("mov %%gs:8, %%rax" : "=a"(cpu) : : "memory");
  return cpu;
}

static inline long hdr_arch_cpu_get_id(void) {
  long id;
  asm volatile("mov %%gs:24, %%rax" : "=a"(id) : : "memory");
  return id;
}

static inline void hdr_arch_cpu_pause(void) {
  asm volatile("pause");
}

static inline void hdr_arch_cpu_interrupt_disable(void) {
  asm volatile("cli");
}

static inline void hdr_arch_cpu_interrupt_enable(void) {
  asm volatile("sti");
}
