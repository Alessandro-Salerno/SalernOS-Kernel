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
#include <kernel/platform/x86-64/apic.h>
#include <kernel/platform/x86-64/arch/mmu.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/msr.h>
#include <lib/printf.h>
#include <stddef.h>
#include <stdint.h>

#define LAPIC_ICR_EDGE      0x00000
#define LAPIC_ICR_DEST_SELF 0x40000
#define LAPIC_ICR_ASSERT    0x04000

#define BSP_APIC_ADDR (void *)0xfee00000

typedef enum {
    LAPIC_EOI        = 0xb0,
    LAPIC_SIVR       = 0xf0,
    LAPIC_ICR_LOW    = 0x300,
    LAPIC_ICR_HIGH   = 0x310,
    LAPIC_LVT_TIMER  = 0x320,
    LAPIC_DIV_CONF   = 0x3e0,
    LAPIC_INIT_COUNT = 0x380,
    LAPIC_CURR_COUNT = 0x390,
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
    return (uint16_t)low | ((uint16_t)high << 8);
}

void x86_64_lapic_eoi(com_isr_t *isr) {
    (void)isr;
    lapic_write(LAPIC_EOI, 0);
}

static void calibrate(void) {
    lapic_write(LAPIC_LVT_TIMER, 1 << 16);
    lapic_write(LAPIC_DIV_CONF, 0);

    X86_64_IO_OUTB(0x43, 0x34);
    X86_64_IO_OUTB(0x40, 0xff);
    X86_64_IO_OUTB(0x40, 0xff);

    uint64_t delta    = 32768;
    uint16_t start    = pit_read();
    uint16_t meas_pit = start;

    lapic_write(LAPIC_INIT_COUNT, 0xffffffff);

    while (true) {
        meas_pit = 0xffff - pit_read();

        if ((uint16_t)(meas_pit - start) >= delta) {
            break;
        }
    }

    uint64_t meas_lapic = 0xffffffff - lapic_read(LAPIC_CURR_COUNT);
    TicksPerSec         = (meas_lapic * 1193182UL) / meas_pit;
}

void x86_64_lapic_bsp_init(void) {
    arch_mmu_map(ARCH_CPU_GET()->root_page_table,
                 (void *)ARCH_PHYS_TO_HHDM(BSP_APIC_ADDR),
                 BSP_APIC_ADDR,
                 ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                     ARCH_MMU_FLAGS_NOEXEC);

    calibrate();
}

void x86_64_lapic_init(void) {
    lapic_write(LAPIC_SIVR, 0x1ff);
    lapic_write(LAPIC_LVT_TIMER, 0x30);
    lapic_write(LAPIC_DIV_CONF, 0);
    lapic_write(LAPIC_LVT_TIMER, 0x30 | 0x20000);
    lapic_write(LAPIC_INIT_COUNT,
                ((1000000UL * TicksPerSec) + 1000000000UL - 1) / 100000000UL);
}
