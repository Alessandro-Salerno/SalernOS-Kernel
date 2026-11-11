#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <kernel/platform/context.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/cr.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <kernel/platform/x86-64/lapic.h>
#include <kernel/platform/x86-64/mmu.h>
#include <kernel/platform/x86-64/smp.h>
#include <kernel/platform/x86-64/syscall.h>
#include <kernel/platform/x86-64/tsc.h>
#include <vendor/limine.h>
#include <vendor/tailq.h>

#define EFER_SYSCALLENABLE 1
#define MAX_CPUS           (PAGE_SIZE * 2) / sizeof(Cpu)

used __attribute__((
    section(".limine_response"))) static volatile struct limine_smp_request
    g_smp_request = (struct limine_smp_request){.id = LIMINE_SMP_REQUEST,
                                                .revision = 0};

static int g_sentinel;
static u8 g_temporary_stack[512 * MAX_CPUS];

static Cpu *g_cpus;
static usize g_num_cpus;

static void common_cpu_init(struct limine_smp_info *cpu_info) {
    cpu_disable_interrupts();
    Cpu *cpu = (void *)cpu_info->extra_argument;
    cpu->runqueue_lock = SPINLOCK_NEW();
    cpu_set(cpu);
    log_debug("initializing cpu %zu", cpu->id);

    mmu_init_cpu();
    gdt_init();
    idt_init();
    idt_reload();
    idt_set_user_invocable(0x80);
    cpu_set_interrupt_stack(cpu, &g_temporary_stack[(cpu->id + 1) * 512 - 1]);

    log("initializing sce");
    u64 efer = msr_read(MSR_EFER);
    efer |= EFER_SYSCALLENABLE;
    msr_write(MSR_EFER, efer);

    u64 star = 0;
    star |= (u64)0x13 << 48;
    star |= (u64)0x08 << 32;

    msr_write(MSR_STAR, star);
    msr_write(MSR_LSTAR, (u64)syscall_sce_entry);
    msr_write(MSR_CSTAR,
              0);                // no compatibility mode syscall handler
    msr_write(MSR_FMASK, 0x200); // disable interrupts on syscall

    log("initializing sse");
    cr_w0((cr_r0() & ~(1 << 2)) | (1 << 1));
    cr_w4(cr_r4() | (3 << 9));

    sched_init();
    TAILQ_INIT(&cpu->sched_queue);

    vmm_switch(NULL);

    TAILQ_INIT(&cpu->callout.queue);
    lapic_init();
}

static void cpu_init(struct limine_smp_info *cpu_info) {
    common_cpu_init(cpu_info);
    tsc_init();

    Cpu *cpu = (void *)cpu_info->extra_argument;

    __atomic_store_n(&g_sentinel, 1, __ATOMIC_SEQ_CST);

    // We avoid taking locks here to make sure we don't accidentally enable
    // interrupts
    cpu->idle_thread->cpu = cpu;
    thread_transition_nolock(cpu->idle_thread, THREAD_STATE_RUNNING);
    cpu->thread = cpu->idle_thread;
    cpu_set_interrupt_stack(cpu, cpu->idle_thread->kernel_stack);

    context_trampoline(&cpu->idle_thread->ctx);
}

void smp_init(void) {
    log("initializng smp");
    assert(sizeof(Cpu) < PAGE_SIZE);
    struct limine_smp_response *smp = g_smp_request.response;

    if (smp == NULL) {
        log_urgent("unable to setup smp");
        return;
    }

    Cpu *cpus = (void *)PHYS_TO_HHDM(pmm_alloczn(2));
    usize avail_cpus = min(MAX_CPUS, smp->cpu_count);
    g_cpus = cpus;
    g_num_cpus = avail_cpus;
    log_debug("found %zu cpus, using max = %zu, initializing %zu cpus",
              smp->cpu_count, MAX_CPUS, avail_cpus);

    Cpu *caller_cpu = cpu_get();

    for (usize i = 0; i < avail_cpus; i++) {
        struct limine_smp_info *info = smp->cpus[i];
        Cpu *cpu = &cpus[i];
        cpu->lapic_id = info->lapic_id;
        cpu->id = i;
        cpu->root_page_table = caller_cpu->root_page_table;
        info->extra_argument = (u64)cpu;

        if (info->lapic_id == smp->bsp_lapic_id) {
            *cpu = *caller_cpu;
            common_cpu_init(info);
            callout_set_bsp_nolock(&cpu->callout);
            continue;
        }

        __atomic_store_n(&g_sentinel, 0, __ATOMIC_SEQ_CST);
        info->goto_address = cpu_init;

        while (__atomic_load_n(&g_sentinel, __ATOMIC_SEQ_CST) == 0) {
            asm("pause");
        }
    }
}

Cpu *smp_get_random(void) {
    static Spinlock lock = SPINLOCK_NEW();
    static int i = 0;
    spinlock_acq(&lock);
    Cpu *ret = &g_cpus[i % g_num_cpus];
    i = (i + 1) % g_num_cpus;
    spinlock_rel(&lock);
    return ret;
}

Cpu *smp_get_cpu(usize cpu) {
    if (cpu < g_num_cpus) return &g_cpus[cpu];

    return NULL;
}
