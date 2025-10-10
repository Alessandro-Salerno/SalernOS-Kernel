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
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/opt/flanterm.h>
#include <kernel/opt/nvme.h>
#include <kernel/opt/uacpi.h>
#include <kernel/platform/x86-64/cr.h>
#include <kernel/platform/x86-64/e9.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/lapic.h>
#include <kernel/platform/x86-64/mmu.h>
#include <kernel/platform/x86-64/msr.h>
#include <kernel/platform/x86-64/ps2.h>
#include <kernel/platform/x86-64/smp.h>
#include <kernel/platform/x86-64/syscall.h>
#include <kernel/platform/x86-64/tsc.h>
#include <vendor/printf.h>
#include <lib/str.h>
#include <lib/util.h>
#include <vendor/limine.h>
#include <vendor/tailq.h>

static arch_cpu_t BspCpu = {0};

static void cpu_panic(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
    (void)ctx;
    printf("halting cpu %d\n", ARCH_CPU_GET()->id);

    ARCH_CPU_DISABLE_INTERRUPTS();
    while (true) {
        ARCH_CPU_HALT();
    }
}

void x86_64_entry(void) {
    // BSP CPU initialization
    x86_64_tsc_boot();
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

    // PHASE 1: memory, interrupts, and processors
    com_init_memory();
    x86_64_idt_stub();
    com_sys_interrupt_register(X86_64_LAPIC_TIMER_INTERRUPT,
                               com_sys_callout_isr,
                               x86_64_lapic_eoi);
    com_sys_interrupt_register(ARCH_CPU_IPI_RESCHEDULE,
                               com_sys_sched_isr,
                               x86_64_lapic_eoi);
    com_isr_t *pf_iser = com_sys_interrupt_register(0x0E,
                                                    x86_64_mmu_fault_isr,
                                                    NULL);
    pf_iser->flags |= COM_SYS_INTERRUPT_FLAGS_NO_RESET;
    com_isr_t *sig_isr = com_sys_interrupt_register(ARCH_CPU_IPI_SIGNAL,
                                                    NULL,
                                                    x86_64_lapic_eoi);
    sig_isr->flags     = COM_SYS_INTERRUPT_FLAGS_NO_RESET;
    com_sys_interrupt_register(ARCH_CPU_IPI_PANIC, cpu_panic, x86_64_lapic_eoi);
    com_isr_t *inv_isr = com_sys_interrupt_register(X86_64_MMU_IPI_INVALIDATE,
                                                    x86_64_mmu_invalidate_isr,
                                                    x86_64_lapic_eoi);
    inv_isr->flags     = COM_SYS_INTERRUPT_FLAGS_NO_RESET;
    com_sys_syscall_init();
    com_sys_interrupt_register(0x80, x86_64_syscall_isr, NULL);
    x86_64_lapic_bsp_init();
    x86_64_tsc_bsp_init();
    x86_64_smp_init();

    // PHASE 3: user program interface
    com_init_filesystem();
    com_init_tty(opt_flanterm_new_context);

    // PHASE 4: devices
    opt_uacpi_init(0);
    x86_64_ps2_init();
    x86_64_ps2_keyboard_init();
    x86_64_ps2_mouse_init();
    com_init_devices();
    opt_nvme_init();

    com_init_pid1();

    for (;;) {
        asm volatile("hlt");
    }
}
