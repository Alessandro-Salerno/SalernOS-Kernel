#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/init.h>
#include <kernel/com/io/log.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <kernel/opt/flanterm.h>
#include <kernel/opt/nvme.h>
#include <kernel/opt/uacpi.h>
#include <kernel/platform/info.h>
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
#include <signal.h>
#include <vendor/limine.h>
#include <vendor/printf.h>
#include <vendor/tailq.h>

static Cpu g_bsp_cpu = {0};

static const int g_exception_signals[20] = {
    [0] = SIGFPE,   [1] = SIGTRAP, [3] = SIGTRAP, [4] = SIGSEGV,
    [5] = SIGSEGV,  [6] = SIGILL,  [11] = SIGBUS, [12] = SIGBUS,
    [13] = SIGSEGV, [16] = SIGFPE, [17] = SIGBUS, [19] = SIGFPE};

static void user_exception_isr(InterruptHandler *isr, Context *ctx) {
    if (!context_is_user(ctx)) idt_exception_isr(isr, ctx);

    signal_send_to_thread(cpu_get_thread(), g_exception_signals[isr->vec],
                          NULL);
}

static void cpu_panic(InterruptHandler *isr, Context *ctx) {
    (void)isr;
    (void)ctx;
    printf("halting cpu %d\n", cpu_get()->id);

    cpu_disable_interrupts();
    while (true) cpu_halt();
}

void entry(void) {
    // BSP Cpu initialization
    tsc_boot();
    cpu_set(&g_bsp_cpu);
    TAILQ_INIT(&g_bsp_cpu.sched_queue);
    TAILQ_INIT(&g_bsp_cpu.callout.queue);
    g_bsp_cpu.runqueue_lock = SPINLOCK_NEW();
    g_bsp_cpu.callout.lock = SPINLOCK_NEW();
    callout_set_bsp_nolock(&g_bsp_cpu.callout);

#if CONFIG_LOG_USE_SERIAL
    log_set_hook_nolock(e9_putsn);
#endif

#if CONFIG_LOG_USE_SCREEN
    flanterm_init_bootstrap();
    log_set_user_hook_nolock(flanterm_bootstrap_putsn);
#endif

    init_splash();

    // PHASE 1: memory, interrupts, and processors
    init_memory();
    idt_stub();
    for (u64 i = 0; i < 20; i++) {
        if (g_exception_signals[i] != 0) {
            interrupt_register(i, user_exception_isr, NULL);
        }
    }
    interrupt_register(LAPIC_TIMER_INTERRUPT, callout_isr, lapic_eoi);
    interrupt_register(CPU_IPI_RESCHEDULE, sched_isr, lapic_eoi);
    interrupt_register(0x0E, mmu_fault_isr, NULL);
    InterruptHandler *sig_isr = interrupt_register(CPU_IPI_SIGNAL, NULL,
                                                   lapic_eoi);
    sig_isr->flags = INTERRUPT_FLAGS_NO_RESET;
    interrupt_register(CPU_IPI_PANIC, cpu_panic, lapic_eoi);
    InterruptHandler *inv_isr = interrupt_register(
        MMU_IPI_INVALIDATE, mmu_invalidate_isr, lapic_eoi);
    inv_isr->flags = INTERRUPT_FLAGS_NO_RESET;
    syscall_init();
    interrupt_register(0x80, syscall_isr, NULL);
    lapic_bsp_init();
    tsc_bsp_init();
    smp_init();

    // PHASE 3: user program interface
    init_filesystem();
    init_tty(flanterm_new_context);

    // Reclaim initrd modules memory
    InfoFile *initrd = info_get_initrd();
    log("reclaiming %zu bytes from initrd", initrd->size);
    pmm_unreserven((void *)HHDM_TO_PHYS(initrd->address),
                   initrd->size / PAGE_SIZE);

    // PHASE 4: devices
    uacpi_init(0);
    ps2_init();
    ps2_keyboard_init();
    ps2_mouse_init();
    init_devices();
    nvme_init();

    init_pid1();

    for (;;) asm volatile("hlt");
}
