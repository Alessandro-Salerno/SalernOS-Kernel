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
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/apic.h>
#include <kernel/platform/x86-64/e9.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <kernel/platform/x86-64/msr.h>
#include <lib/printf.h>
#include <vendor/limine.h>

static arch_cpu_t            BaseCpu           = {0};
static uint8_t               KernelStack[4096] = {0};
static arch_context_t        UserCtx           = {0};
static arch_mmu_pagetable_t *UserPt            = NULL;
static void                 *UserStack         = NULL;
static bool                  Ready             = false;

USER_DATA volatile const char *user_message =
    "Hello from SalernOS userspace!\n";

USER_TEXT void user_test(void) {
  while (1) {
    asm volatile("movq $0x0, %%rax\n"
                 "movq %0, %%rdi\n"
                 "int $0x80\n"
                 :
                 : "b"(user_message)
                 : "%rax", "%rdi", "%rcx", "%rdx", "memory");
  }
}

USED static void sched(com_isr_t *isr, arch_context_t *ctx) {
  (void)isr;
  (void)ctx;
  if (!Ready) {
    return;
  }

  DEBUG("fake rescheduling %x", hdr_arch_cpu_get());
  hdr_x86_64_msr_write(X86_64_MSR_KERNELGSBASE, (uint64_t)NULL);
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
  com_sys_interrupt_register(0x30, sched, x86_64_lapic_eoi);

  UserCtx.cs = 0x20 | 3;
  UserCtx.ss = 0x18 | 3;
  UserPt     = arch_mmu_new_table();
  UserStack  = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  arch_mmu_map(UserPt,
               UserStack,
               (void *)ARCH_HHDM_TO_PHYS(UserStack),
               ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                   ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);
  UserCtx.rsp                  = (uint64_t)UserStack + 4095;
  hdr_arch_cpu_get()->ist.rsp0 = (uint64_t)&KernelStack[4095];
  UserCtx.rip                  = (uint64_t)user_test;
  UserCtx.rflags               = (1ul << 1) | (1ul << 9) | (1ul << 21);
  arch_mmu_switch(UserPt);
  Ready = true;
  arch_ctx_trampoline(&UserCtx);

  // intentional page fault
  // *(volatile int *)NULL = 2;
  // asm volatile("int $0x80");
  // *((volatile int *)NULL) = 3;
  while (1)
    ;
}
