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
#include <arch/info.h>
#include <kernel/com/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/apic.h>
#include <kernel/platform/x86-64/e9.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <lib/printf.h>
#include <vendor/limine.h>

static arch_cpu_t BaseCpu = {0};

__attribute__((section(".user_data"))) volatile const char *tester =
    "Hello from SalernOS userspace";

__attribute__((section(".user_text"))) void user_test(void) {
  asm volatile("movq $0x0, %%rax\n"
               "movq %0, %%rdi\n"
               "int $0x80\n"
               :
               : "b"(tester)
               : "%rax", "%rdi", "%rcx", "%rdx", "memory");

  while (1)
    ;
}

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

  void *slab_1 = com_mm_slab_alloc(256);
  DEBUG("slab_1=%x", slab_1);

  x86_64_lapic_bsp_init();
  x86_64_lapic_init();

  com_sys_syscall_init();
  x86_64_idt_set_user_invocable(0x80);

  arch_context_t ctx = {0};
  ctx.cs             = 0x20 | 3;
  ctx.ss             = 0x18 | 3;
  void *ustack       = com_mm_pmm_alloc();
  arch_mmu_map(hdr_arch_cpu_get()->root_page_table,
               (void *)ARCH_PHYS_TO_HHDM(ustack),
               ustack,
               ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                   ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);
  ctx.rsp = ARCH_PHYS_TO_HHDM(ustack) + 4095;
  asm volatile("movq %%rsp, %%rax"
               : "=a"(hdr_arch_cpu_get()->ist.rsp0)
               :
               : "memory");
  ctx.rip = (uint64_t)user_test;
  // ctx.rflags = (1ul << 1) | (1ul << 9) | (1ul << 21);
  arch_ctx_switch(&ctx);

  // intentional page fault
  // *(volatile int *)NULL = 2;
  // asm volatile("int $0x80");
  // *((volatile int *)NULL) = 3;
  while (1)
    ;
}
