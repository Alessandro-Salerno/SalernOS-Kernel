#pragma once

#include <kernel/com/mm/pmmcache.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/platform/x86-64/arch/mmu.h>
#include <kernel/platform/x86-64/ist.h>
#include <kernel/platform/x86-64/lapic.h>
#include <kernel/platform/x86-64/msr.h>
#include <kernel/platform/x86-64/tsc.h>
#include <vendor/tailq.h>

#define CPU_IPI_RESCHEDULE 0x31
#define CPU_IPI_SIGNAL     0x32
#define CPU_IPI_PANIC      0x33

typedef struct Cpu {
    // Must be here
    struct Thread *thread;
    struct Cpu *self;
    u64 id;
    u32 lapic_id;

    u64 gdt[7];
    InterruptStackTable ist;
    u64 tsc_reverse_mult;
    PMMCache mmu_cache;
    usize *mmu_shootdown_counter;
    Spinlock mmu_shootdown_lock;
    void *mmu_shootdown_virt;
    usize mmu_shootdown_pages;

    MMUPageTable *root_page_table;

    struct ThreadSList zombies; // Cpu-local, accessed with interrupts disabled
    Spinlock runqueue_lock;
    struct ThreadTailQ sched_queue;
    struct CalloutQueue callout;
    struct Thread *idle_thread;
} Cpu;

static inline Cpu *cpu_get(void) {
    Cpu *cpu;
    asm volatile("mov %%gs:8, %%rax" : "=a"(cpu) : : "memory");
    return cpu;
}

static inline struct Thread *cpu_get_thread(void) {
    struct Thread *thread;
    asm volatile("mov %%gs:0, %%rax" : "=a"(thread) : : "memory");
    return thread;
}

static inline u64 cpu_get_id(void) {
    u64 id;
    asm volatile("mov %%gs:16, %%rax" : "=a"(id) : : "memory");
    return id;
}

static inline u32 cpu_get_hardware_id(void) {
    u32 hw_id;
    asm volatile("mov %%gs:24, %%rax" : "=a"(hw_id) : : "memory");
    return hw_id;
}

static inline void cpu_set(Cpu *cpu) {
    cpu->self = cpu;
    msr_write(MSR_GSBASE, (u64)cpu);
    msr_write(MSR_KERNELGSBASE, (u64)cpu);
}

static inline void cpu_set_interrupt_stack(Cpu *cpu, void *kstack) {
    cpu->ist.rsp0 = (u64)kstack;
}

static inline void cpu_pause(void) { asm volatile("pause"); }
static inline void cpu_disable_interrupts(void) { asm volatile("cli"); }
static inline void cpu_enable_interrupts(void) { asm volatile("sti"); }
static inline void cpu_halt(void) { asm volatile("hlt"); }

static inline void cpu_self_ipi(u8 ipi) { lapic_selfipi(ipi); }
static inline void cpu_broadcast_ipi(u8 ipi) { lapic_broadcast_ipi(ipi); }
static inline void cpu_send_ipi(Cpu *dst_cpu, u8 ipi) {
    lapic_send_ipi(dst_cpu->lapic_id, ipi);
}

static inline u64 cpu_get_timestamp(void) { return tsc_read(); }
static inline u64 cpu_timestamp_to_ns(u64 ts) {
    return tsc_val_to_ns(cpu_get()->tsc_reverse_mult, ts);
}
static inline u64 cpu_get_time(void) {
    return tsc_get_ns(cpu_get()->tsc_reverse_mult);
}
