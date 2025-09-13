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

#pragma once

#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/x86-64/apic.h>
#include <kernel/platform/x86-64/arch/mmu.h>
#include <kernel/platform/x86-64/ist.h>
#include <kernel/platform/x86-64/msr.h>
#include <lib/spinlock.h>
#include <stdbool.h>
#include <stdint.h>
#include <vendor/tailq.h>

#define ARCH_CPU_SET_INTERRUPT_STACK(cpu, kstack) \
    (cpu)->ist.rsp0 = (uint64_t)kstack

#define ARCH_CPU_GET()        __hdr_arch_cpu_get()
#define ARCH_CPU_GET_THREAD() __hdr_arch_cpu_get_thread()
#define ARCH_CPU_GET_ID()     __hdr_arch_cpu_get_id()

#define ARCH_CPU_PAUSE() asm volatile("pause")

#define ARCH_CPU_DISABLE_INTERRUPTS() asm volatile("cli")
#define ARCH_CPU_ENABLE_INTERRUPTS()  asm volatile("sti")
#define ARCH_CPU_SELF_IPI()           x86_64_lapic_selfipi()
#define ARCH_CPU_SEND_IPI(dst_cpu) \
    x86_64_lapic_send_ipi((dst_cpu)->lapic_id, X86_64_LAPIC_SELF_IPI_INTERRUPT);

#define ARCH_CPU_HALT() asm volatile("hlt");

typedef struct arch_cpu {
    // Must be here
    struct com_thread *thread;
    struct arch_cpu   *self;

    uint64_t     id;
    uint64_t     gdt[7];
    x86_64_ist_t ist;
    uint32_t     lapic_id;
    // NOTE: keeping this per core since some older CPUs may do weird things
    uint64_t tsc_freq;

    arch_mmu_pagetable_t *root_page_table;

    kspinlock_t              runqueue_lock;
    struct com_thread_tailq  sched_queue;
    struct com_callout_queue callout;
    struct com_thread       *idle_thread;
} arch_cpu_t;

#define ARCH_CPU_SET(cpuptr)                                 \
    (cpuptr)->self = (cpuptr);                               \
    X86_64_MSR_WRITE(X86_64_MSR_GSBASE, (uint64_t)(cpuptr)); \
    X86_64_MSR_WRITE(X86_64_MSR_KERNELGSBASE, (uint64_t)(cpuptr))

static inline arch_cpu_t *__hdr_arch_cpu_get(void) {
    arch_cpu_t *cpu;
    asm volatile("mov %%gs:8, %%rax" : "=a"(cpu) : : "memory");
    return cpu;
}

static inline struct com_thread *__hdr_arch_cpu_get_thread(void) {
    struct com_thread *thread;
    asm volatile("mov %%gs:0, %%rax" : "=a"(thread) : : "memory");
    return thread;
}

static inline long __hdr_arch_cpu_get_id(void) {
    long id;
    asm volatile("mov %%gs:24, %%rax" : "=a"(id) : : "memory");
    return id;
}
