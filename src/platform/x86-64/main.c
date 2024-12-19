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
#include <arch/info.h>
#include <kernel/com/fs/initrd.h>
#include <kernel/com/fs/tmpfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/platform/info.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/apic.h>
#include <kernel/platform/x86-64/e9.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <kernel/platform/x86-64/msr.h>
#include <lib/mem.h>
#include <lib/printf.h>
#include <stdint.h>
#include <vendor/limine.h>
#include <vendor/tailq.h>

static arch_cpu_t BaseCpu = {0};

USED static void sched(com_isr_t *isr, arch_context_t *ctx) {
  (void)isr;
  com_sys_sched_run(ctx);
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

  x86_64_lapic_bsp_init();
  x86_64_lapic_init();

  com_sys_syscall_init();
  x86_64_idt_set_user_invocable(0x80);
  com_sys_interrupt_register(0x30, sched, x86_64_lapic_eoi);

  com_vfs_t *rootfs = NULL;
  com_fs_tmpfs_mount(&rootfs, NULL);

  arch_file_t *initrd = arch_info_get_initrd();
  com_fs_initrd_make(rootfs->root, initrd->address, initrd->size);

  com_vnode_t *myfile_txt = NULL;
  com_fs_vfs_lookup(&myfile_txt, "/myfile.txt", 11, rootfs->root, rootfs->root);
  DEBUG("found /myfile.txt at: %x", myfile_txt);
  ASSERT(NULL != myfile_txt);

  char *buf = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  kmemset(buf, ARCH_PAGE_SIZE, 0);
  com_fs_vfs_read(buf, 5, NULL, myfile_txt, 0, 0);
  DEBUG("reading from /myfile.txt: %s", buf);

  arch_mmu_pagetable_t *user_pt = arch_mmu_new_table();
  void                 *ustack  = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  arch_mmu_map(user_pt,
               ustack,
               (void *)ARCH_HHDM_TO_PHYS(ustack),
               ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                   ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);

  void *ustack2 = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  arch_mmu_map(user_pt,
               ustack2,
               (void *)ARCH_HHDM_TO_PHYS(ustack2),
               ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                   ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);

  com_elf_data_t elf_data = {0};
  ASSERT(0 ==
         com_sys_elf64_load(
             &elf_data, "/test", 5, rootfs->root, rootfs->root, 0, user_pt));
  DEBUG("elf entry at %x", elf_data.entry);

  com_proc_t   *proc = com_sys_proc_new(user_pt, 0);
  com_thread_t *thread =
      com_sys_thread_new(proc, ustack, ARCH_PAGE_SIZE, (void *)elf_data.entry);

  com_proc_t   *proc2   = com_sys_proc_new(user_pt, 0);
  com_thread_t *thread2 = com_sys_thread_new(
      proc2, ustack2, ARCH_PAGE_SIZE, (void *)elf_data.entry);
  // TODO: all of this shoud be done by fork
  thread2->ctx.rsp -= sizeof(thread2->ctx);
  kmemcpy((void *)thread2->ctx.rsp, &thread2->ctx, sizeof(thread2->ctx));
  ((arch_context_t *)thread2->ctx.rsp)->rsp =
      (uint64_t)ustack2 + ARCH_PAGE_SIZE;
  thread2->ctx.rsp -= 8;
  *(uint64_t *)thread2->ctx.rsp = (uint64_t)x86_64_ctx_test_trampoline;

  hdr_arch_cpu_get()->ist.rsp0 = (uint64_t)thread->kernel_stack;
  hdr_arch_cpu_get()->thread   = thread;

  TAILQ_INSERT_TAIL(&BaseCpu.sched_queue, thread2, threads);

  arch_mmu_switch(proc->page_table);
  arch_ctx_trampoline(&thread->ctx);

  for (;;) {
    asm volatile("hlt");
  }
}
