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
#include <kernel/platform/x86-64/cpuid.h>
#include <kernel/platform/x86-64/pit.h>
#include <kernel/platform/x86-64/tsc.h>
#include <lib/util.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define TSC_REQUIRED_LEAF       0x80000007
#define TSC_CPUID_AVAIL         (1 << 8)
#define TSC_CALIBRATE_PIT_TICKS (32768)

// From what I could understand, if the TSC is invariant, then it ticks at a
// constant rate across all cores, so we can calibrate it just once Of course
// all of this is terribly stupid because it is not backwards compatible AND
// relies on PIT, which is not precise enough

// NOTE: not static so we can access them from the header which is better for
// inlining
uint64_t __x86_64_TSC_BootValue = 0;
uint64_t __x86_64_TSC_BootTime  = 0;

static bool tsc_probe(void) {
    uint32_t max_leaf = X86_64_CPUID_EXT_MAX_LEAF();
    if (max_leaf < TSC_REQUIRED_LEAF) {
        return false;
    }

    x86_64_cpuid_t cpuid;
    X86_64_CPUID(&cpuid, TSC_REQUIRED_LEAF);
    return TSC_CPUID_AVAIL & cpuid.edx;
}

void x86_64_tsc_boot(void) {
    __x86_64_TSC_BootValue = X86_64_TSC_READ();
}

void x86_64_tsc_bsp_init(void) {
    KASSERT(0 == __x86_64_TSC_BootTime);
    x86_64_tsc_init();
    __x86_64_TSC_BootTime = X86_64_TSC_VAL_TO_NS(ARCH_CPU_GET()->tsc_freq,
                                                 __x86_64_TSC_BootValue);
}

void x86_64_tsc_init(void) {
    if (!tsc_probe()) {
        // Set some random but realistic frequency for TSC
        ARCH_CPU_GET()->tsc_freq = 3 * 1000UL * 1000UL * 1000UL;
        return;
    }

    KLOG("initializing tsc");
    arch_cpu_t *curr_cpu = ARCH_CPU_GET();

    if (X86_64_CPUID_BASE_MAX_LEAF() >= 0x15) {
        x86_64_cpuid_t regs;
        X86_64_CPUID(&regs, 0x15);

        if (0 == regs.eax || 0 == regs.ebx || 0 == regs.ecx) {
            goto calibrate;
        }

        uint64_t core_freq        = regs.ecx;
        uint64_t core_numerator   = regs.ebx;
        uint64_t core_denominator = regs.eax;
        curr_cpu->tsc_freq = core_freq * core_numerator / core_denominator;
        return;
    }

calibrate:;
    uintmax_t start_tsc = X86_64_TSC_READ();
    uint64_t  meas_pit  = x86_64_pit_wait_ticks(TSC_CALIBRATE_PIT_TICKS);
    uintmax_t end_tsc   = X86_64_TSC_READ();
    curr_cpu->tsc_freq  = (end_tsc - start_tsc) / meas_pit *
                         X86_64_PIT_FREQUENCY;

    KDEBUG("tsc has %zu hz measured with pit=%zu",
           curr_cpu->tsc_freq,
           meas_pit);
}
