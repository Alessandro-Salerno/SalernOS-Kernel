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

#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/init.h>
#include <kernel/com/io/log.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/opt/flanterm.h>
#include <kernel/opt/uacpi.h>
#include <kernel/platform/x86-64/apic.h>
#include <kernel/platform/x86-64/cr.h>
#include <kernel/platform/x86-64/e9.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/msr.h>
#include <kernel/platform/x86-64/ps2.h>
#include <kernel/platform/x86-64/smp.h>
#include <lib/printf.h>
#include <lib/str.h>
#include <lib/util.h>
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

void x86_64_entry(void) {
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

    com_init_splash();

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

    // PHASE 1: memory, interrupts, and processors
    com_init_memory();
    x86_64_idt_stub();
    com_sys_interrupt_register(X86_64_LAPIC_TIMER_INTERRUPT,
                               com_sys_callout_isr,
                               x86_64_lapic_eoi);
    com_sys_interrupt_register(X86_64_LAPIC_SELF_IPI_INTERRUPT,
                               com_sys_sched_isr,
                               x86_64_lapic_eoi);
    com_sys_interrupt_register(0x0E, pgf_sig_test, NULL);
    com_sys_syscall_init();
    x86_64_lapic_bsp_init();
    x86_64_smp_init();

    // PHASE 3: user program interface
    com_init_filesystem();
    x86_64_ps2_init();
    x86_64_ps2_keyboard_init();
    x86_64_ps2_mouse_init();
    com_init_tty(opt_flanterm_new_context);
    com_init_devices();

    // opt_uacpi_init(0);
    com_init_pid1();

    for (;;) {
        asm volatile("hlt");
    }
}
