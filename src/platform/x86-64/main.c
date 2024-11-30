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

#include <arch/cpu.h>
#include <arch/mmu.h>
#include <kernel/com/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/platform/x86-64/e9.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <lib/printf.h>
#include <vendor/limine.h>

static arch_cpu_t BaseCpu = {0};

void kernel_entry(void) {
  hdr_arch_cpu_set(&BaseCpu);
  com_log_set_hook(x86_64_e9_putc);
  x86_64_gdt_init();
  x86_64_idt_init();
  x86_64_idt_reload();
  com_mm_pmm_init();
  arch_mmu_init();

  // pmm tests
  void *a = com_mm_pmm_alloc();
  void *b = com_mm_pmm_alloc();
  DEBUG("a=%x b=%x", a, b);
  com_mm_pmm_free(b);
  void *c = com_mm_pmm_alloc();
  com_mm_pmm_free(a);
  void *d = com_mm_pmm_alloc();
  DEBUG("c=%x d=%x", c, d);
  com_mm_pmm_free(c);
  com_mm_pmm_free(d);

  // intentional page fault
  // *(volatile int *)NULL = 2;
  // asm volatile("int $0x80");
  // *((volatile int *)NULL) = 3;
  while (1)
    ;
}
