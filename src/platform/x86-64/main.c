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
#include <threads.h>
#include <vendor/limine.h>
#include <vendor/tailq.h>

#include "arch/context.h"
#include "arch/mmu.h"
#include "com/sys/proc.h"
#include "com/sys/thread.h"

static arch_cpu_t BaseCpu = {0};

USER_DATA volatile const char *user_message =
    "Hello from SalernOS userspace!\n";

USER_TEXT void user_test(void) {
  asm volatile("movq $0x0, %%rax\n"
               "movq %0, %%rdi\n"
               "int $0x80\n"
               :
               : "b"(user_message)
               : "%rax", "%rdi", "%rcx", "%rdx", "memory");

  while (1)
    ;
}

USED static void sched(com_isr_t *isr, arch_context_t *ctx) {
  (void)isr;
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

  arch_context_t *prev_regs = &curr->ctx;
  prev_regs->rax            = ctx->rax;
  prev_regs->rbx            = ctx->rbx;
  prev_regs->rcx            = ctx->rcx;
  prev_regs->rdx            = ctx->rdx;
  prev_regs->r8             = ctx->r8;
  prev_regs->r9             = ctx->r9;
  prev_regs->r10            = ctx->r10;
  prev_regs->r11            = ctx->r11;
  prev_regs->r12            = ctx->r12;
  prev_regs->r13            = ctx->r13;
  prev_regs->r14            = ctx->r14;
  prev_regs->r15            = ctx->r15;
  prev_regs->rdi            = ctx->rdi;
  prev_regs->rsi            = ctx->rsi;
  prev_regs->rbp            = ctx->rbp;
  prev_regs->rip            = ctx->rip;
  prev_regs->rsp            = ctx->rsp;

  arch_context_t *next_regs = &next->ctx;
  hdr_x86_64_msr_write(X86_64_MSR_KERNELGSBASE, (uint64_t)next_regs->gs);
  ctx->rax = next_regs->rax;
  ctx->rbx = next_regs->rbx;
  ctx->rcx = next_regs->rcx;
  ctx->rdx = next_regs->rdx;
  ctx->r8  = next_regs->r8;
  ctx->r9  = next_regs->r9;
  ctx->r10 = next_regs->r10;
  ctx->r11 = next_regs->r11;
  ctx->r12 = next_regs->r12;
  ctx->r13 = next_regs->r13;
  ctx->r14 = next_regs->r14;
  ctx->r15 = next_regs->r15;
  ctx->rdi = next_regs->rdi;
  ctx->rsi = next_regs->rsi;
  ctx->rbp = next_regs->rbp;
  ctx->rip = next_regs->rip;
  ctx->rsp = next_regs->rsp;

  cpu->ist.rsp0 = (uint64_t)next->kernel_stack;
}

void kernel_entry(void) {
  hdr_arch_cpu_set(&BaseCpu);
  TAILQ_INIT(&BaseCpu.sched_queue);
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

  arch_context_t ctx            = {0};
  ctx.cs                        = 0x20 | 3;
  ctx.ss                        = 0x18 | 3;
  arch_mmu_pagetable_t *user_pt = arch_mmu_new_table();
  void                 *ustack  = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  arch_mmu_map(user_pt,
               ustack,
               (void *)ARCH_HHDM_TO_PHYS(ustack),
               ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                   ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);
  ctx.rsp              = (uint64_t)ustack + 4095;
  ctx.rip              = (uint64_t)user_test;
  ctx.rflags           = (1ul << 1) | (1ul << 9) | (1ul << 21);
  com_proc_t   *proc   = (com_proc_t *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  com_thread_t *thread = (com_thread_t *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  thread->proc         = proc;
  thread->runnable     = true;
  thread->ctx          = ctx;
  thread->kernel_stack = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  proc->page_table     = user_pt;
  proc->pid            = 1;
  hdr_arch_cpu_get()->ist.rsp0 = (uint64_t)thread->kernel_stack;
  TAILQ_INSERT_HEAD(&BaseCpu.sched_queue, thread, threads);
  arch_mmu_switch(user_pt);
  arch_ctx_trampoline(&ctx);

  // intentional page fault
  // *(volatile int *)NULL = 2;
  // asm volatile("int $0x80");
  // *((volatile int *)NULL) = 3;
  while (1)
    ;
}
