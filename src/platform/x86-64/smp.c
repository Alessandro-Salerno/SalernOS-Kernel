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
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/sched.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/cr.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <kernel/platform/x86-64/lapic.h>
#include <kernel/platform/x86-64/smp.h>
#include <kernel/platform/x86-64/tsc.h>
#include <lib/mem.h>
#include <lib/spinlock.h>
#include <lib/util.h>
#include <stddef.h>
#include <vendor/limine.h>
#include <vendor/tailq.h>

#define MAX_CPUS ARCH_PAGE_SIZE / sizeof(arch_cpu_t)

__attribute__((
    used,
    section(".limine_response"))) static volatile struct limine_smp_request
    SmpRequest = (struct limine_smp_request){.id       = LIMINE_SMP_REQUEST,
                                             .revision = 0};

static int     Sentinel;
static uint8_t TemporaryStack[512 * MAX_CPUS];

static arch_cpu_t *Cpus;
static size_t      NumCpus;

static void common_cpu_init(struct limine_smp_info *cpu_info) {
    ARCH_CPU_DISABLE_INTERRUPTS();
    arch_cpu_t *cpu    = (void *)cpu_info->extra_argument;
    cpu->runqueue_lock = KSPINLOCK_NEW();
    ARCH_CPU_SET(cpu);
    KDEBUG("initializing cpu %u", cpu->id);

    x86_64_gdt_init();
    x86_64_idt_init();
    x86_64_idt_reload();
    x86_64_idt_set_user_invocable(0x80);
    ARCH_CPU_SET_INTERRUPT_STACK(cpu, &TemporaryStack[(cpu->id + 1) * 512 - 1]);

    KLOG("initializing sse");
    X86_64_CR_W0((X86_64_CR_R0() & ~(1 << 2)) | (1 << 1));
    X86_64_CR_W4(X86_64_CR_R4() | (3 << 9));

    com_sys_sched_init();
    TAILQ_INIT(&cpu->sched_queue);

    arch_mmu_switch_default();

    TAILQ_INIT(&cpu->callout.queue);
    x86_64_lapic_init();
}

static void cpu_init(struct limine_smp_info *cpu_info) {
    common_cpu_init(cpu_info);
    x86_64_tsc_init();

    arch_cpu_t *cpu = (void *)cpu_info->extra_argument;

    __atomic_store_n(&Sentinel, 1, __ATOMIC_SEQ_CST);
    cpu->idle_thread->lock_depth = 0;
    cpu->thread                  = cpu->idle_thread;
    ARCH_CPU_SET_INTERRUPT_STACK(cpu, cpu->idle_thread->kernel_stack);

    asm("sti");
    while (1) {
        asm("sti");
        asm("hlt");
    }
}

void x86_64_smp_init(void) {
    KLOG("initializng smp");
    KASSERT(sizeof(arch_cpu_t) < ARCH_PAGE_SIZE);
    struct limine_smp_response *smp = SmpRequest.response;

    if (NULL == smp) {
        KURGENT("unable to setup smp");
        return;
    }

    arch_cpu_t *cpus       = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc_zero());
    size_t      avail_cpus = KMIN(MAX_CPUS, smp->cpu_count);
    Cpus                   = cpus;
    NumCpus                = avail_cpus;
    KDEBUG("found %u cpus, using max = %u, initializing %u cpus",
           smp->cpu_count,
           MAX_CPUS,
           avail_cpus);

    arch_cpu_t *caller_cpu = ARCH_CPU_GET();

    for (size_t i = 0; i < avail_cpus; i++) {
        struct limine_smp_info *info = smp->cpus[i];
        arch_cpu_t             *cpu  = &cpus[i];
        cpu->lapic_id                = info->lapic_id;
        cpu->id                      = i;
        cpu->root_page_table         = caller_cpu->root_page_table;
        info->extra_argument         = (uint64_t)cpu;

        if (info->lapic_id == smp->bsp_lapic_id) {
            *cpu = *caller_cpu;
            common_cpu_init(info);
            com_sys_callout_set_bsp_nolock(&cpu->callout);
            continue;
        }

        __atomic_store_n(&Sentinel, 0, __ATOMIC_SEQ_CST);
        info->goto_address = cpu_init;

        while (0 == __atomic_load_n(&Sentinel, __ATOMIC_SEQ_CST)) {
            asm("pause");
        }
    }
}

arch_cpu_t *x86_64_smp_get_random(void) {
    static kspinlock_t lock = KSPINLOCK_NEW();
    static int         i    = 0;
    kspinlock_acquire(&lock);
    arch_cpu_t *ret = &Cpus[i % NumCpus];
    i               = (i + 1) % NumCpus;
    kspinlock_release(&lock);
    return ret;
}

arch_cpu_t *x86_64_smp_get_cpu(size_t cpu) {
    return &Cpus[cpu];
}
