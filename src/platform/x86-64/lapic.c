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

#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/arch/mmu.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/lapic.h>
#include <kernel/platform/x86-64/msr.h>
#include <lib/printf.h>
#include <stddef.h>
#include <stdint.h>

#define LAPIC_ICR_EDGE      0x00000
#define LAPIC_ICR_DEST_SELF 0x40000
#define LAPIC_ICR_ASSERT    0x04000

#define BSP_APIC_ADDR (void *)0xfee00000

typedef enum {
    E_LAPIC_REG_EOI        = 0xb0,
    E_LAPIC_REG_SIVR       = 0xf0,
    E_LAPIC_REG_ICR_LOW    = 0x300,
    E_LAPIC_REG_ICR_HIGH   = 0x310,
    E_LAPIC_REG_LVT_TIMER  = 0x320,
    E_LAPIC_REG_DIV_CONF   = 0x3e0,
    E_LAPIC_REG_INIT_COUNT = 0x380,
    E_LAPIC_REG_CURR_COUNT = 0x390,
} lapic_reg_t;

static uint64_t TicksPerSec;

static void lapic_write(uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)(ARCH_PHYS_TO_HHDM(BSP_APIC_ADDR) + offset) = value;
}

static uint32_t lapic_read(uint32_t offset) {
    return *(volatile uint32_t *)(ARCH_PHYS_TO_HHDM(BSP_APIC_ADDR) + offset);
}

static uint16_t pit_read(void) {
    X86_64_IO_OUTB(0x43, 0);
    uint8_t low  = X86_64_IO_INB(0x40);
    uint8_t high = X86_64_IO_INB(0x40);
    return low | high << 8;
}

void x86_64_lapic_eoi(com_isr_t *isr) {
    (void)isr;
    lapic_write(E_LAPIC_REG_EOI, 0);
}

static void lapic_calibrate(void) {
    lapic_write(E_LAPIC_REG_LVT_TIMER, 1UL << 16);
    lapic_write(E_LAPIC_REG_DIV_CONF, 0);

    X86_64_IO_OUTB(0x43, 0x34);
    X86_64_IO_OUTB(0x40, 0xff);
    X86_64_IO_OUTB(0x40, 0xff);

    uint64_t delta    = 32768;
    uint16_t start    = 0xffff - pit_read();
    uint16_t meas_pit = start;

    lapic_write(E_LAPIC_REG_INIT_COUNT, 0xffffffff);

    while (true) {
        meas_pit = 0xffff - pit_read();

        if ((uint16_t)(meas_pit - start) >= delta) {
            break;
        }
    }

    uint64_t meas_lapic = 0xffffffff - lapic_read(E_LAPIC_REG_CURR_COUNT);
    TicksPerSec         = (meas_lapic * 1193182UL) / meas_pit;
}

static void periodic(uint64_t ns, size_t interrupt) {
    lapic_write(E_LAPIC_REG_LVT_TIMER, interrupt | 0x20000);
    lapic_write(E_LAPIC_REG_INIT_COUNT,
                ((ns * TicksPerSec) + 1000000000UL - 1) / 1000000000UL);
}

void x86_64_lapic_bsp_init(void) {
    arch_mmu_map(ARCH_CPU_GET()->root_page_table,
                 (void *)ARCH_PHYS_TO_HHDM(BSP_APIC_ADDR),
                 BSP_APIC_ADDR,
                 ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                     ARCH_MMU_FLAGS_NOEXEC);

    lapic_calibrate();
}

void x86_64_lapic_init(void) {
    lapic_write(E_LAPIC_REG_SIVR, (1UL << 8) | 0xff);
    lapic_write(E_LAPIC_REG_LVT_TIMER, X86_64_LAPIC_TIMER_INTERRUPT);
    lapic_write(E_LAPIC_REG_DIV_CONF, 0);
    periodic(ARCH_TIMER_NS, X86_64_LAPIC_TIMER_INTERRUPT);
}

void x86_64_lapic_selfipi(void) {
    while (lapic_read(E_LAPIC_REG_ICR_LOW) & 0x1000);
    lapic_write(E_LAPIC_REG_ICR_LOW,
                X86_64_LAPIC_SELF_IPI_INTERRUPT | LAPIC_ICR_DEST_SELF |
                    LAPIC_ICR_ASSERT | LAPIC_ICR_EDGE);
    lapic_write(E_LAPIC_REG_ICR_HIGH, 0);
}

void x86_64_lapic_send_ipi(uint8_t apic_id, uint8_t vector) {
    // Wait until the previous IPI has been sent
    while (lapic_read(E_LAPIC_REG_ICR_LOW) & 0x1000);

    // Set the target APIC ID in the high ICR register
    lapic_write(E_LAPIC_REG_ICR_HIGH, ((uint32_t)apic_id) << 24);

    // Construct the IPI command in the low ICR register
    lapic_write(E_LAPIC_REG_ICR_LOW,
                vector | LAPIC_ICR_ASSERT |
                    LAPIC_ICR_EDGE); // Fixed delivery mode by default (000)

    // Wait for the IPI to be delivered
    while (lapic_read(E_LAPIC_REG_ICR_LOW) & 0x1000);
}
