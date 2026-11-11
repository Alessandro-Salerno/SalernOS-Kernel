#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/arch/mmu.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/lapic.h>
#include <kernel/platform/x86-64/msr.h>
#include <kernel/platform/x86-64/pit.h>
#include <vendor/printf.h>

#define LAPIC_ICR_EDGE      0x00000
#define LAPIC_ICR_DEST_SELF 0x40000
#define LAPIC_ICR_ASSERT    0x04000

#define BSP_APIC_ADDR (void *)0xfee00000

typedef enum {
    LAPIC_REG_EOI = 0xb0,
    LAPIC_REG_SIVR = 0xf0,
    LAPIC_REG_ICR_LOW = 0x300,
    LAPIC_REG_ICR_HIGH = 0x310,
    LAPIC_REG_LVT_TIMER = 0x320,
    LAPIC_REG_DIV_CONF = 0x3e0,
    LAPIC_REG_INIT_COUNT = 0x380,
    LAPIC_REG_CURR_COUNT = 0x390,
} LAPICReg;

static u64 g_ticks_per_sec;

static void lapic_write(u32 offset, u32 value) {
    *(volatile u32 *)(PHYS_TO_HHDM(BSP_APIC_ADDR) + offset) = value;
}

static u32 lapic_read(u32 offset) {
    return *(volatile u32 *)(PHYS_TO_HHDM(BSP_APIC_ADDR) + offset);
}

void lapic_eoi(InterruptHandler *isr) {
    (void)isr;
    lapic_write(LAPIC_REG_EOI, 0);
}

static void lapic_calibrate(void) {
    lapic_write(LAPIC_REG_LVT_TIMER, 1UL << 16);
    lapic_write(LAPIC_REG_DIV_CONF, 0);
    lapic_write(LAPIC_REG_INIT_COUNT, 0xffffffff);

    u16 meas_pit = pit_wait_ticks(32768);
    u64 meas_lapic = 0xffffffff - lapic_read(LAPIC_REG_CURR_COUNT);
    g_ticks_per_sec = (meas_lapic * PIT_FREQUENCY) / meas_pit;
}

static void periodic(u64 ns, usize interrupt) {
    lapic_write(LAPIC_REG_LVT_TIMER, interrupt | 0x20000);
    lapic_write(LAPIC_REG_INIT_COUNT,
                ((ns * g_ticks_per_sec) + 1000000000UL - 1) / 1000000000UL);
}

void lapic_bsp_init(void) {
    vmm_map(NULL, NULL, BSP_APIC_ADDR, PAGE_SIZE,
            VMM_FLAGS_NOHINT | VMM_FLAGS_PHYSICAL,
            MMU_FLAGS_READ | MMU_FLAGS_WRITE | MMU_FLAGS_NOEXEC);

    lapic_calibrate();
}

void lapic_init(void) {
    lapic_write(LAPIC_REG_SIVR, (1UL << 8) | 0xff);
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_TIMER_INTERRUPT);
    lapic_write(LAPIC_REG_DIV_CONF, 0);
    periodic(TIMER_NS, LAPIC_TIMER_INTERRUPT);
}

void lapic_selfipi(u8 vector) {
    while (lapic_read(LAPIC_REG_ICR_LOW) & 0x1000);
    lapic_write(LAPIC_REG_ICR_LOW,
                vector | LAPIC_ICR_DEST_SELF | LAPIC_ICR_ASSERT |
                    LAPIC_ICR_EDGE);
    lapic_write(LAPIC_REG_ICR_HIGH, 0);
}

void lapic_send_ipi(u8 apic_id, u8 vector) {
    // Wait until the previous IPI has been sent
    while (lapic_read(LAPIC_REG_ICR_LOW) & 0x1000);

    // Set the target APIC ID in the high ICR register
    lapic_write(LAPIC_REG_ICR_HIGH, ((u32)apic_id) << 24);

    // Construct the IPI command in the low ICR register
    lapic_write(LAPIC_REG_ICR_LOW,
                vector | LAPIC_ICR_ASSERT |
                    LAPIC_ICR_EDGE); // Fixed delivery mode by default (000)

    // Wait for the IPI to be delivered
    while (lapic_read(LAPIC_REG_ICR_LOW) & 0x1000);
}

void lapic_broadcast_ipi(u8 vector) {
    // Wait until the previous IPI has been sent
    while (lapic_read(LAPIC_REG_ICR_LOW) & 0x1000);

    // high not used in broadcast shorthand
    lapic_write(LAPIC_REG_ICR_HIGH, 0);
    lapic_write(LAPIC_REG_ICR_LOW,
                vector | LAPIC_ICR_ASSERT | LAPIC_ICR_EDGE | (0b11 << 18));

    // Wait for the IPI to be delivered
    while (lapic_read(LAPIC_REG_ICR_LOW) & 0x1000);
}
