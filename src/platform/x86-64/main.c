/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
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
#include <kernel/com/io/console.h>
#include <kernel/com/io/log.h>
#include <kernel/com/io/term.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/signal.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/opt/flanterm.h>
#include <kernel/platform/context.h>
#include <kernel/platform/info.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/apic.h>
#include <kernel/platform/x86-64/cr.h>
#include <kernel/platform/x86-64/e9.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/msr.h>
#include <kernel/platform/x86-64/smp.h>
#include <lib/hashmap.h>
#include <lib/mem.h>
#include <lib/printf.h>
#include <lib/str.h>
#include <lib/util.h>
#include <signal.h>
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

static char asciitableShifted[] = {
    0,    '\033', '!', '@',  '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+',
    '\b', '\t',   'Q', 'W',  'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{',  '}',
    '\r', 0,      'A', 'S',  'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"',  '~',
    0,    '|',    'Z', 'X',  'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,    '*',
    0,    ' ',    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',    '8', '9',  '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.',
    0,    0,      '<', 0,    0,   0,   0,   0,   0,   0,   0,   0,   '\r', 0,
    '/',  0,      0,   '\r', 0,   0,   0,   0,   0,   0,   0,   0,   0,    0};

USED void kbd(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
    (void)ctx;
    static uint8_t   prev_code = 0;
    static uintmax_t mod       = 0;
    uint8_t          code      = X86_64_IO_INB(0x60);

    if (0xe0 == prev_code) {
        if (0x53 == code) {
            com_io_console_kbd_in(127, 0);
            goto end;
        }

        switch (code) {
            case 0x4b:
                com_io_console_kbd_in('D', COM_IO_TTY_MOD_ARROW);
                break;

            case 0x4d:
                com_io_console_kbd_in('C', COM_IO_TTY_MOD_ARROW);
                break;

            case 0x48:
                com_io_console_kbd_in('A', COM_IO_TTY_MOD_ARROW);
                break;

            case 0x50:
                com_io_console_kbd_in('B', COM_IO_TTY_MOD_ARROW);
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
                if (code >= 0x3b && code <= 0x44) {
                    int  fkey = code - 0x3b;
                    char key_c =
                        'A' + fkey; // TODO: this is not standard, has to be
                                    // translated in tty to be sent to userspace
                    com_io_console_kbd_in(key_c, mod | COM_IO_TTY_MOD_FKEY);
                } else if (code < 0x80) {
                    if (COM_IO_TTY_MOD_LSHIFT & mod ||
                        COM_IO_TTY_MOD_RSHIFT & mod) {
                        com_io_console_kbd_in(asciitableShifted[code], mod);
                    } else {
                        com_io_console_kbd_in(asciitable[code], mod);
                    }
                }
                break;
        }
    }

end:
    prev_code = code;
}

USED void kbd_eoi(com_isr_t *isr) {
    (void)isr;
    X86_64_IO_OUTB(0x20, 0x20);
}

USED void pgf_sig_test(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL == curr_thread) {
        com_panic(ctx, "cpu exception (page fault)");
    }
    com_proc_t *curr_proc = curr_thread->proc;

    if (ARCH_CONTEXT_ISUSER(ctx) && NULL != curr_proc) {
        KDEBUG("sending SIGSEGV to pid=%d", curr_proc->pid);
        if (NULL != curr_proc->fd[2].file &&
            NULL != curr_proc->fd[2].file->vnode) {
            char *message = "SEGMENTATION FAULT\n";
            com_fs_vfs_write(NULL,
                             curr_proc->fd[2].file->vnode,
                             message,
                             kstrlen(message),
                             curr_proc->fd[2].file->off,
                             0);
        }
        com_sys_signal_send_to_proc(curr_proc->pid, SIGSEGV, NULL);
    } else {
        com_panic(ctx, "cpu exception (page fault)");
    }
}

void kernel_entry(void) {
    ARCH_CPU_SET(&BaseCpu);
    TAILQ_INIT(&BaseCpu.sched_queue);
    TAILQ_INIT(&BaseCpu.callout.queue);
    BaseCpu.runqueue_lock = COM_SPINLOCK_NEW();

    X86_64_IO_OUTB(0x20, 0x11);
    X86_64_IO_OUTB(0xa0, 0x11);
    X86_64_IO_OUTB(0x21, 0x20);
    X86_64_IO_OUTB(0xa1, 0x28);
    X86_64_IO_OUTB(0x21, 4);
    X86_64_IO_OUTB(0xa1, 2);
    X86_64_IO_OUTB(0x21, 0x01);
    X86_64_IO_OUTB(0xa1, 0x01);
    X86_64_IO_OUTB(0x21, 0b11111101);
    X86_64_IO_OUTB(0xA1, 0b11111111);
    com_sys_interrupt_register(0x21, kbd, kbd_eoi);

#ifndef X86_64_NO_E9_LOG
    com_io_log_set_hook(x86_64_e9_putc);
#else
    com_io_log_set_hook(com_io_fbterm_putc);
#endif
    com_mm_pmm_init();
    arch_mmu_init();
    KDEBUG("sizeof(com_proc_t) = %d", sizeof(com_proc_t));

    x86_64_idt_stub();
    com_sys_syscall_init();

    com_term_backend_t main_tb   = opt_flanterm_new_context();
    com_term_t        *main_term = com_io_term_new(main_tb);
    com_io_term_set_fallback(main_term);

    com_sys_interrupt_register(
        X86_64_LAPIC_TIMER_INTERRUPT, com_sys_callout_isr, x86_64_lapic_eoi);
    com_sys_interrupt_register(
        X86_64_LAPIC_SELF_IPI_INTERRUPT, com_sys_sched_isr, x86_64_lapic_eoi);
    com_sys_interrupt_register(0x0E, pgf_sig_test, NULL);
    x86_64_lapic_bsp_init();
    x86_64_smp_init();

    com_vfs_t *rootfs = NULL;
    com_fs_tmpfs_mount(&rootfs, NULL);

    com_vnode_t *tmpfs_mountpoint = NULL;
    com_vfs_t   *tmpfs            = NULL;
    com_fs_tmpfs_mkdir(
        &tmpfs_mountpoint, rootfs->root, "tmp", kstrlen("tmp"), 0);
    com_fs_tmpfs_mount(&tmpfs, tmpfs_mountpoint);

    arch_file_t *initrd = arch_info_get_initrd();
    com_fs_initrd_make(rootfs->root, initrd->address, initrd->size);

    com_vfs_t *devfs = NULL;
    com_fs_devfs_init(&devfs, rootfs);

    com_vnode_t *main_tty_dev  = NULL;
    com_tty_t   *main_tty_data = NULL;
    com_io_tty_init_text(&main_tty_dev, &main_tty_data, main_term);
    com_io_term_init();
    com_io_console_add_tty(main_tty_data);

    for (size_t i = 0; i < COM_IO_CONSOLE_MAX_TTYS - 1; i++) {
        com_term_backend_t tb   = opt_flanterm_new_context();
        com_term_t        *term = com_io_term_new(tb);

        com_vnode_t *tty_dev  = NULL;
        com_tty_t   *tty_data = NULL;
        com_io_tty_init_text(&tty_dev, &tty_data, term);
        com_io_term_set_buffering(term, true);
        com_io_console_add_tty(tty_data);
    }

    char *const   argv[] = {"/boot/init", NULL};
    char *const   envp[] = {NULL};
    com_proc_t   *proc = com_sys_proc_new(NULL, 0, rootfs->root, rootfs->root);
    com_thread_t *thread = com_sys_thread_new(proc, NULL, 0, NULL);
    KASSERT(
        0 ==
        com_sys_elf64_prepare_proc(
            &proc->page_table, "/boot/init", argv, envp, proc, &thread->ctx));

    com_file_t *stdfile = com_mm_slab_alloc(sizeof(com_file_t));
    stdfile->vnode      = main_tty_dev;
    stdfile->num_ref    = 3;
    proc->next_fd       = 3;
    com_filedesc_t stddesc;
    stddesc.file  = stdfile;
    stddesc.flags = 0;
    proc->fd[0]   = stddesc;
    proc->fd[1]   = stddesc;
    proc->fd[2]   = stddesc;

    com_sys_proc_new_session_nolock(proc, main_tty_dev);

    ARCH_CPU_GET()->ist.rsp0 = (uint64_t)thread->kernel_stack;
    ARCH_CPU_GET()->thread   = thread;

    arch_mmu_switch(proc->page_table);
    ARCH_CONTEXT_RESTORE_EXTRA(thread->xctx);

    TAILQ_INSERT_TAIL(&BaseCpu.sched_queue, thread, threads);
    com_sys_sched_init_base();
    com_io_term_enable(main_term);
    com_io_term_set_buffering(main_term, true);
    arch_context_trampoline(&thread->ctx);

    for (;;) {
        asm volatile("hlt");
    }
}
