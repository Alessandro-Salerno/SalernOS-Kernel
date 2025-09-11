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
#include <kernel/com/dev/gfx/fbdev.h>
#include <kernel/com/dev/null.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/initrd.h>
#include <kernel/com/fs/tmpfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/console.h>
#include <kernel/com/io/log.h>
#include <kernel/com/io/pty.h>
#include <kernel/com/io/term.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
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
#include <kernel/platform/x86-64/ps2.h>
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

static arch_cpu_t BspCpu = {0};

KUSED void pgf_sig_test(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL == curr_thread) {
        com_sys_panic(ctx, "cpu exception (page fault)");
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
        com_ipc_signal_send_to_proc(curr_proc->pid, SIGSEGV, NULL);
    } else {
        com_sys_panic(ctx, "cpu exception (page fault)");
    }
}

extern const char _binary_src_splash_txt_start[];
extern const char _binary_src_splash_txt_end[];

void kernel_entry(void) {
    // BSP CPU initialization
    ARCH_CPU_SET(&BspCpu);
    TAILQ_INIT(&BspCpu.sched_queue);
    TAILQ_INIT(&BspCpu.callout.queue);
    BspCpu.runqueue_lock = KSPINLOCK_NEW();
    BspCpu.callout.lock  = KSPINLOCK_NEW();
    com_sys_callout_set_bsp_nolock(&BspCpu.callout);

#if CONFIG_LOG_USE_SERIAL
    com_io_log_set_hook_nolock(x86_64_e9_putsn);
#endif

#if CONFIG_LOG_USE_SCREEN
    opt_flanterm_init_bootstrap();
    com_io_log_set_user_hook_nolock(opt_flanterm_bootstrap_putsn);
#endif

#if CONFIG_LOG_SHOW_SPLASH
    com_io_log_putsn(_binary_src_splash_txt_start,
                     _binary_src_splash_txt_end - _binary_src_splash_txt_start);
    com_io_log_puts("\n\n");
#endif

    // PIC initialization
    X86_64_IO_OUTB(0x20, 0x11);
    X86_64_IO_OUTB(0xA0, 0x11);
    X86_64_IO_OUTB(0x21, 0x20);
    X86_64_IO_OUTB(0xA1, 0x28);
    X86_64_IO_OUTB(0x21, 4);
    X86_64_IO_OUTB(0xA1, 2);
    X86_64_IO_OUTB(0x21, 0x01);
    X86_64_IO_OUTB(0xA1, 0x01);
    X86_64_IO_OUTB(0x21, 0b11111001);
    X86_64_IO_OUTB(0xA1, 0b11101111);

    com_mm_pmm_init();
    arch_mmu_init();
    x86_64_idt_stub();
    x86_64_ps2_init();
    com_sys_syscall_init();

    com_term_backend_t main_tb   = opt_flanterm_new_context();
    com_term_t        *main_term = com_io_term_new(main_tb);
    com_io_term_set_fallback(main_term);

    com_sys_interrupt_register(X86_64_LAPIC_TIMER_INTERRUPT,
                               com_sys_callout_isr,
                               x86_64_lapic_eoi);
    com_sys_interrupt_register(X86_64_LAPIC_SELF_IPI_INTERRUPT,
                               com_sys_sched_isr,
                               x86_64_lapic_eoi);
    com_sys_interrupt_register(0x0E, pgf_sig_test, NULL);
    x86_64_lapic_bsp_init();
    x86_64_smp_init();

    com_vfs_t *rootfs = NULL;
    com_fs_tmpfs_mount(&rootfs, NULL);

    com_vnode_t *tmpfs_mountpoint = NULL;
    com_vfs_t   *tmpfs            = NULL;
    com_fs_tmpfs_mkdir(&tmpfs_mountpoint,
                       rootfs->root,
                       "tmp",
                       kstrlen("tmp"),
                       0,
                       0);
    com_fs_tmpfs_mount(&tmpfs, tmpfs_mountpoint);

    com_vnode_t *procfs_mountpoint = NULL;
    com_vfs_t   *procfs            = NULL;
    com_fs_tmpfs_mkdir(&procfs_mountpoint,
                       rootfs->root,
                       "proc",
                       kstrlen("proc"),
                       0,
                       0);
    com_fs_tmpfs_mount(&procfs, procfs_mountpoint);

    com_vnode_t *proc_kernel = NULL;
    com_fs_tmpfs_mkdir(&proc_kernel,
                       procfs->root,
                       "kernel",
                       kstrlen("kernel"),
                       0,
                       0);

#if CONFIG_LOG_USE_VNODE
#warning "log to vnode is experimental!"
    com_vnode_t *kernel_log = NULL;
    com_fs_tmpfs_create(&kernel_log, proc_kernel, "log", kstrlen("log"), 0, 0);
    com_io_log_set_vnode(kernel_log);
#endif

    arch_file_t *initrd = arch_info_get_initrd();
    com_fs_initrd_make(rootfs->root, initrd->address, initrd->size);

    com_vfs_t *devfs = NULL;
    com_fs_devfs_init(&devfs, rootfs);

    x86_64_ps2_keyboard_init();
    x86_64_ps2_mouse_init();

    com_vnode_t *devtty = NULL;
    com_io_tty_devtty_init(&devtty);

    com_vnode_t *main_tty_dev  = NULL;
    com_tty_t   *main_tty_data = NULL;
    com_io_tty_init_text(&main_tty_dev, &main_tty_data, main_term);
    com_io_term_init();
    com_io_console_add_tty(main_tty_data);

    for (size_t i = 0; i < CONFIG_TTY_MAX - 1; i++) {
        com_term_backend_t tb   = opt_flanterm_new_context();
        com_term_t        *term = com_io_term_new(tb);

        com_vnode_t *tty_dev  = NULL;
        com_tty_t   *tty_data = NULL;
        com_io_tty_init_text(&tty_dev, &tty_data, term);
        com_io_term_set_buffering(term, true);
        com_io_console_add_tty(tty_data);
    }

    com_sys_proc_init();
    com_io_pty_init();
    com_dev_gfx_fbdev_init(NULL);
    com_dev_null_init();

    char *const   argv[] = {CONFIG_INIT_PATH, CONFIG_INIT_ARGV};
    char *const   envp[] = {CONFIG_INIT_ENV};
    com_proc_t   *proc = com_sys_proc_new(NULL, 0, rootfs->root, rootfs->root);
    com_thread_t *thread  = com_sys_thread_new(proc, NULL, 0, NULL);
    int           elf_ret = com_sys_elf64_prepare_proc(&proc->page_table,
                                             CONFIG_INIT_PATH,
                                             argv,
                                             envp,
                                             proc,
                                             &thread->ctx);

    if (0 != elf_ret) {
        com_sys_panic(NULL,
                      "unable to start init program at %s",
                      CONFIG_INIT_PATH);
    }

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

    TAILQ_INSERT_TAIL(&BspCpu.sched_queue, thread, threads);
    com_sys_sched_init_base();
    com_io_log_set_user_hook(NULL);
    com_io_term_enable(main_term);
    com_io_term_set_buffering(main_term, true);
    arch_context_trampoline(&thread->ctx);

    for (;;) {
        asm volatile("hlt");
    }
}
