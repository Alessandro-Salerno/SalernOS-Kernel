#include <arch/context.h>
#include <arch/cpu.h>
#include <kernel/com/io/log.h>
#include <kernel/com/io/term.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/panic.h>
#include <kernel/com/util.h>
#include <kernel/platform/context.h>
#include <stdarg.h>
#include <vendor/printf.h>

noreturn void panic(Context *ctx, const char *fmt, ...) {
    cpu_disable_interrupts();
    // cpu_broadcast_ipi(CPU_IPI_PANIC);
    term_panic();
    Thread *curr_thread = cpu_get_thread();
    printf("kernel panic on cpu %zu\n", cpu_get_id());

    if (fmt != NULL) {
        va_list args;
        va_start(args, fmt);
        vprintf(fmt, args);
        va_end(args);
        printf("\n\n");
    }

    PMMStats memstats;
    pmm_get_stats(&memstats);
    printf("used memory: %zu byte(s)\n", memstats.allocated);
    printf("free memory: %zu byte(s)\n", memstats.free);
    printf("reserved memory: %zu byte(s)\n", memstats.reserved);
    printf("system memory: %zu byte(s)\n", memstats.usable);
    printf("total memory: %zu byte(s)\n", memstats.total);
    printf("\n");

    if (ctx == NULL && curr_thread != NULL) {
        printf("note: cpu context data refers to kernel stack for tid=%d, "
               "since context was not provided\n",
               curr_thread->tid);
        ctx = &curr_thread->ctx;
    }

    if (ctx != NULL) context_print(ctx);

    while (1) {
        cpu_disable_interrupts();
        cpu_halt();
    }
}
