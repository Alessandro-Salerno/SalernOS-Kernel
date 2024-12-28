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
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/initrd.h>
#include <kernel/com/fs/tmpfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/fbterm.h>
#include <kernel/com/io/log.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/context.h>
#include <kernel/platform/info.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/apic.h>
#include <kernel/platform/x86-64/e9.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/msr.h>
#include <lib/mem.h>
#include <lib/printf.h>
#include <lib/util.h>
#include <stdint.h>
#include <stdio.h>
#include <vendor/limine.h>
#include <vendor/tailq.h>

// #define X86_64_NO_E9_LOG

static arch_cpu_t BaseCpu = {0};

// from Astral & ke!
static char asciitable[] = {
    0,    '\033', '1', '2',  '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    '\b', '\t',   'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    '\r', 0,      'a', 's',  'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\',   'z', 'x',  'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
    0,    ' ',    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',    '8', '9',  '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.',
    0,    0,      '<', 0,    0,   0,   0,   0,   0,   0,   0,   0,   '\r', 0,
    '/',  0,      0,   '\r', 0,   0,   0,   0,   0,   0,   0,   0,   0,    0};

USED void kbd(com_isr_t *isr, arch_context_t *ctx) {
  (void)isr;
  (void)ctx;
  static uint8_t   prev_code = 0;
  static uintmax_t mod       = 0;
  uint8_t          code      = hdr_x86_64_io_inb(0x60);

  if (0xe0 == prev_code) {
    if (0x53 == code) {
      com_io_tty_kbd_in(127, 0);
    }

    switch (code) {
    case 0x4b:
      com_io_tty_kbd_in('D', COM_IO_TTY_MOD_ARROW);
      break;

    case 0x4d:
      com_io_tty_kbd_in('C', COM_IO_TTY_MOD_ARROW);
      break;

    case 0x48:
      com_io_tty_kbd_in('A', COM_IO_TTY_MOD_ARROW);
      break;

    case 0x50:
      com_io_tty_kbd_in('B', COM_IO_TTY_MOD_ARROW);
      break;
    }
  } else {
    switch (code) {
    case 0x2a:
      mod = mod | COM_IO_TTY_MOD_LSHIFT;
      break;

    case 0xaa:
      mod = mod & ~COM_IO_TTY_MOD_LSHIFT;
      break;

    case 0x36:
      mod = mod | COM_IO_TTY_MOD_RSHIFT;
      break;

    case 0xb6:
      mod = mod & ~COM_IO_TTY_MOD_RSHIFT;
      break;

    case 0x1d:
      mod = mod | COM_IO_TTY_MOD_LCTRL;
      break;

    case 0x9d:
      mod = mod & ~COM_IO_TTY_MOD_LCTRL;
      break;

    case 0x38:
      mod = mod | COM_IO_TTY_MOD_LALT;
      break;

    case 0xb8:
      mod = mod & ~COM_IO_TTY_MOD_LALT;
      break;

    default:
      if (code < 0x80) {
        com_io_tty_kbd_in(asciitable[code], mod);
      }
      break;
    }
  }

  prev_code = code;
}

USED void kbd_eoi(com_isr_t *isr) {
  (void)isr;
  hdr_x86_64_io_outb(0x20, 0x20);
}

void kernel_entry(void) {
  hdr_arch_cpu_set(&BaseCpu);
  TAILQ_INIT(&BaseCpu.sched_queue);

  hdr_x86_64_io_outb(0x20, 0x11);
  hdr_x86_64_io_outb(0xa0, 0x11);
  hdr_x86_64_io_outb(0x21, 0x20);
  hdr_x86_64_io_outb(0xa1, 0x28);
  hdr_x86_64_io_outb(0x21, 4);
  hdr_x86_64_io_outb(0xa1, 2);
  hdr_x86_64_io_outb(0x21, 0x01);
  hdr_x86_64_io_outb(0xa1, 0x01);
  hdr_x86_64_io_outb(0x21, 0b11111101);
  hdr_x86_64_io_outb(0xA1, 0b11111111);
  com_sys_interrupt_register(0x21, kbd, kbd_eoi);

  arch_framebuffer_t *fb = arch_info_get_fb();
  com_io_fbterm_init(fb);

#ifndef X86_64_NO_E9_LOG
  com_io_log_set_hook(x86_64_e9_putc);
#else
  com_io_log_set_hook(com_io_fbterm_putc);
#endif

  x86_64_gdt_init();
  x86_64_idt_init();
  x86_64_idt_reload();
  com_mm_pmm_init();
  arch_mmu_init();

  com_sys_syscall_init();
  x86_64_idt_set_user_invocable(0x80);

  com_vfs_t *rootfs = NULL;
  com_fs_tmpfs_mount(&rootfs, NULL);

  arch_file_t *initrd = arch_info_get_initrd();
  com_fs_initrd_make(rootfs->root, initrd->address, initrd->size);

  com_vnode_t *myfile_txt = NULL;
  com_fs_vfs_lookup(&myfile_txt, "/myfile.txt", 11, rootfs->root, rootfs->root);
  KDEBUG("found /myfile.txt at: %x", myfile_txt);
  KASSERT(NULL != myfile_txt);

  char *buf = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  kmemset(buf, ARCH_PAGE_SIZE, 0);
  com_fs_vfs_read(buf, 5, NULL, myfile_txt, 0, 0);
  KDEBUG("reading from /myfile.txt: %s", buf);

  com_vfs_t *devfs = NULL;
  com_fs_devfs_init(&devfs, rootfs);

  com_vnode_t *tty_dev = NULL;
  com_io_tty_init(&tty_dev);

  arch_mmu_pagetable_t *user_pt = arch_mmu_new_table();
  void                 *ustack  = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  arch_mmu_map(user_pt,
               ustack,
               (void *)ARCH_HHDM_TO_PHYS(ustack),
               ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                   ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);
  com_elf_data_t elf_data = {0};
  KASSERT(0 ==
          com_sys_elf64_load(
              &elf_data, "/test", 5, rootfs->root, rootfs->root, 0, user_pt));
  KDEBUG("elf entry at %x", elf_data.entry);
  KDEBUG("enf interpreter path: %s", elf_data.interpreter_path);

  com_proc_t *proc = com_sys_proc_new(user_pt, 0, rootfs->root, rootfs->root);
  com_file_t *stdfile = com_mm_slab_alloc(sizeof(com_file_t));
  stdfile->vnode      = tty_dev;
  stdfile->num_ref    = 3;
  proc->next_fd       = 3;
  com_filedesc_t stddesc;
  stddesc.file  = stdfile;
  stddesc.flags = 0;
  proc->fd[0]   = stddesc;
  proc->fd[1]   = stddesc;
  proc->fd[2]   = stddesc;
  com_thread_t *thread =
      com_sys_thread_new(proc, ustack, ARCH_PAGE_SIZE, (void *)elf_data.entry);

  hdr_arch_cpu_get()->ist.rsp0 = (uint64_t)thread->kernel_stack;
  hdr_arch_cpu_get()->thread   = thread;

  com_sys_sched_init();
  x86_64_lapic_bsp_init();
  x86_64_lapic_init();
  com_sys_interrupt_register(0x30, com_sys_sched_isr, x86_64_lapic_eoi);

  arch_mmu_switch(proc->page_table);
  arch_context_trampoline(&thread->ctx);

  KDEBUG("intstatus: %u", BaseCpu.intstatus);
  for (;;) {
    asm volatile("hlt");
  }
}
