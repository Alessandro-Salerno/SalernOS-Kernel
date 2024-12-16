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
//
// USER_DATA volatile const char *Proc1Message = "Hello from process 1!\n";
//
// USER_DATA volatile const char *Proc2Message = "Hello from process 2!\n";
//
// USER_TEXT void proc1entry(void) {
//   while (1) {
//     asm volatile("movq $0x0, %%rax\n"
//                  "movq %0, %%rdi\n"
//                  "int $0x80\n"
//                  :
//                  : "b"(Proc1Message)
//                  : "%rax", "%rdi", "%rcx", "%rdx", "memory");
//   }
// }
//
// USER_TEXT void proc2entry(void) {
//   while (1) {
//     asm volatile("movq $0x0, %%rax\n"
//                  "movq %0, %%rdi\n"
//                  "int $0x80\n"
//                  :
//                  : "b"(Proc2Message)
//                  : "%rax", "%rdi", "%rcx", "%rdx", "memory");
//   }
// }
//
// USED static void sched(com_isr_t *isr, arch_context_t *ctx) {
//   (void)isr;
//   com_sys_sched_run(ctx);
// }

void kernel_entry(void) {
  hdr_arch_cpu_set(&BaseCpu);
  TAILQ_INIT(&BaseCpu.sched_queue);
  com_log_set_hook(x86_64_e9_putc);
  x86_64_gdt_init();
  x86_64_idt_init();
  x86_64_idt_reload();
  com_mm_pmm_init();
  arch_mmu_init();

  // x86_64_lapic_bsp_init();
  // x86_64_lapic_init();

  com_sys_syscall_init();
  x86_64_idt_set_user_invocable(0x80);
  // com_sys_interrupt_register(0x30, sched, x86_64_lapic_eoi);

  // arch_mmu_pagetable_t *user_pt = arch_mmu_new_table();
  // void                 *ustack  = (void
  // *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc()); arch_mmu_map(user_pt,
  //              ustack,
  //              (void *)ARCH_HHDM_TO_PHYS(ustack),
  //              ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
  //                  ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);
  //
  // com_proc_t   *proc = com_sys_proc_new(user_pt, 0);
  // com_thread_t *thread =
  //     com_sys_thread_new(proc, ustack, ARCH_PAGE_SIZE, proc1entry);
  //
  // user_pt = arch_mmu_new_table();
  // ustack  = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  // arch_mmu_map(user_pt,
  //              ustack,
  //              (void *)ARCH_HHDM_TO_PHYS(ustack),
  //              ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
  //                  ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);
  // com_proc_t   *proc2 = com_sys_proc_new(user_pt, 0);
  // com_thread_t *thread2 =
  //     com_sys_thread_new(proc, ustack, ARCH_PAGE_SIZE, proc2entry);
  //
  // hdr_arch_cpu_get()->ist.rsp0 = (uint64_t)thread->kernel_stack;
  // hdr_arch_cpu_get()->thread   = thread;
  //
  // TAILQ_INSERT_TAIL(&BaseCpu.sched_queue, thread2, threads);
  //
  // arch_mmu_switch(proc->page_table);
  // arch_ctx_trampoline(&thread->ctx);

  com_vfs_t *root = NULL;
  com_fs_tmpfs_mount(&root, NULL);

  arch_file_t *initrd = arch_info_get_initrd();
  com_fs_initrd_make(root->root, initrd->address, initrd->size);
  DEBUG("gets here");

  // com_vnode_t *newfile = NULL;
  // com_fs_vfs_create(&newfile, root->root, "myfile.txt", 10, 0);
  // ASSERT(NULL != newfile);

  com_vnode_t *samefile = NULL;
  com_fs_vfs_lookup(&samefile, "/myfile.txt", 11, root->root, root->root);
  DEBUG("found file at: %x", samefile);
  ASSERT(NULL != samefile);
  // DEBUG("orig=%x, lookup=%x", newfile, samefile);
  // ASSERT(newfile == samefile);

  size_t dump = 0;
  // DEBUG("writing 'ciao' to /myfile.txt");
  // com_fs_vfs_write(&dump, samefile, "ciao", 4, 0, 0);
  char *buf = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  kmemset(buf, ARCH_PAGE_SIZE, 0);
  com_fs_vfs_read(buf, 5, &dump, samefile, 0, 0);
  DEBUG("reading from /myfile.txt: %s", buf);

  // intentional page fault
  // *(volatile int *)NULL = 2;
  // asm volatile("int $0x80");
  // *((volatile int *)NULL) = 3;
  while (1)
    ;
}
