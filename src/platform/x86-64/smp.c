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
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/sched.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/apic.h>
#include <kernel/platform/x86-64/cr.h>
#include <kernel/platform/x86-64/gdt.h>
#include <kernel/platform/x86-64/idt.h>
#include <kernel/platform/x86-64/smp.h>
#include <lib/mem.h>
#include <lib/util.h>
#include <stddef.h>
#include <vendor/limine.h>
#include <vendor/tailq.h>

#define MAX_CPUS ARCH_PAGE_SIZE / sizeof(arch_cpu_t)

__attribute__((
    used,
    section(".limine_response"))) static volatile struct limine_smp_request
    SmpRequest =
        (struct limine_smp_request){.id = LIMINE_SMP_REQUEST, .revision = 0};

static int     Sentinel;
static uint8_t TemporaryStack[512 * MAX_CPUS];

static void common_cpu_init(struct limine_smp_info *cpu_info) {
    hdr_arch_cpu_interrupt_disable();
    arch_cpu_t *cpu    = (void *)cpu_info->extra_argument;
    cpu->runqueue_lock = COM_SPINLOCK_NEW();
    hdr_arch_cpu_set(cpu);
    KDEBUG("initializing cpu %u", cpu->id);

    x86_64_gdt_init();
    x86_64_idt_init();
    x86_64_idt_reload();
    x86_64_idt_set_user_invocable(0x80);
    ARCH_CPU_SET_KERNEL_STACK(cpu, &TemporaryStack[(cpu->id + 1) * 512 - 1]);

    KLOG("initializing sse on cpu %u", cpu->id);
    hdr_x86_64_cr_write0((hdr_x86_64_cr_read0() & ~(1 << 2)) | (1 << 1));
    hdr_x86_64_cr_write4(hdr_x86_64_cr_read4() | (3 << 9));

    com_sys_sched_init();
    TAILQ_INIT(&cpu->sched_queue);
    TAILQ_INIT(&cpu->zombie_queue);

    arch_mmu_switch_default();

    x86_64_lapic_init();
}

static void cpu_init(struct limine_smp_info *cpu_info) {
    common_cpu_init(cpu_info);
    arch_cpu_t *cpu = (void *)cpu_info->extra_argument;

    __atomic_store_n(&Sentinel, 1, __ATOMIC_SEQ_CST);
    cpu->idle_thread->lock_depth = 0;
    cpu->thread                  = cpu->idle_thread;
    ARCH_CPU_SET_KERNEL_STACK(cpu, cpu->idle_thread->kernel_stack);

    asm("sti");
    while (1) {
        asm("sti");
        asm("hlt");
    }
}

arch_cpu_t *Cpus;
size_t      NumCpus;

void x86_64_smp_init(void) {
    KLOG("initializng smp");
    KASSERT(sizeof(arch_cpu_t) < ARCH_PAGE_SIZE);
    struct limine_smp_response *smp = SmpRequest.response;

    if (NULL == smp) {
        KLOG("unable to setup smp");
        return;
    }

    arch_cpu_t *cpus       = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
    size_t      avail_cpus = KMIN(MAX_CPUS, smp->cpu_count);
    Cpus                   = cpus;
    NumCpus                = avail_cpus;
    kmemset(cpus, ARCH_PAGE_SIZE, 0);
    KDEBUG("found %u cpus, using max = %u, initializing %u cpus",
           smp->cpu_count,
           MAX_CPUS,
           avail_cpus);

    for (size_t i = 0; i < avail_cpus; i++) {
        struct limine_smp_info *info = smp->cpus[i];
        arch_cpu_t             *cpu  = &cpus[i];
        cpu->id                      = i;
        info->extra_argument         = (uint64_t)cpu;

        if (info->lapic_id == smp->bsp_lapic_id) {
            common_cpu_init(info);
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
    static com_spinlock_t lock = COM_SPINLOCK_NEW();
    static int            i    = 0;
    com_spinlock_acquire(&lock);
    arch_cpu_t *ret = &Cpus[i % NumCpus];
    i               = (i + 1) % NumCpus;
    com_spinlock_release(&lock);
    return ret;
}

arch_cpu_t *x86_64_smp_get_cpu(size_t cpu) {
    return &Cpus[cpu];
}
