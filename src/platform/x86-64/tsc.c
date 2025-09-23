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
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/sched.h>
#include <kernel/platform/x86-64/cpuid.h>
#include <kernel/platform/x86-64/tsc.h>
#include <lib/util.h>
#include <stdatomic.h>
#include <stdbool.h>

#define TSC_REQUIRED_LEAF        0x80000007
#define TSC_CPUID_AVAIL          (1 << 8)
#define TSC_CALIBRATE_TIME_NS    (ARCH_TIMER_NS * 32)
#define TSC_CALIBRATE_TIME_MILLS (TSC_CALIBRATE_TIME_NS / 1000000UL)

// From what I could understand, if the TSC is invariant, then it ticks at a
// constant rate across all cores, so we can calibrate it just once Of course
// all of this is terribly stupid because it is not backwards compatible AND
// relies on callout timer, which is very very unreliable
static uintmax_t   StartCount   = 0;
static uintmax_t   EndCount     = 0;
static atomic_bool DoneSentinel = false;

// NOTE: not static so we can access them from the header which is better for
// inlining
uintmax_t __x86_64_TSC_Frequency = 0;
uint64_t  __x86_64_TSC_BootValue = 0;

static bool tsc_probe(void) {
    uint32_t max_leaf = X86_64_CPUID_EXT_MAX_LEAF();
    if (max_leaf < TSC_REQUIRED_LEAF) {
        return false;
    }

    x86_64_cpuid_t cpuid;
    X86_64_CPUID(&cpuid, TSC_REQUIRED_LEAF);
    return TSC_CPUID_AVAIL & cpuid.edx;
}

static void tsc_calibrate_timeout(com_callout_t *callout) {
    (void)callout;
    EndCount = X86_64_TSC_READ();
    atomic_store(&DoneSentinel, true);
}

void x86_64_tsc_boot(void) {
    __x86_64_TSC_BootValue = X86_64_TSC_READ();
}

void x86_64_tsc_init(void) {
    if (!tsc_probe()) {
        return;
    }

    KLOG("initializing tsc");

    if (X86_64_CPUID_BASE_MAX_LEAF() >= 0x15) {
        x86_64_cpuid_t regs;
        X86_64_CPUID(&regs, 0x15);

        if (0 == regs.eax || 0 == regs.ebx || 0 == regs.ecx) {
            goto calibrate;
        }

        uint64_t core_freq        = regs.ecx;
        uint64_t core_numerator   = regs.ebx;
        uint64_t core_denominator = regs.eax;
        ARCH_CPU_GET()->tsc_freq  = core_freq * core_numerator /
                                   core_denominator;
        return;
    }

calibrate:
    StartCount = X86_64_TSC_READ();
    com_sys_callout_add(tsc_calibrate_timeout, NULL, TSC_CALIBRATE_TIME_NS);
    ARCH_CPU_ENABLE_INTERRUPTS();
    while (!atomic_load(&DoneSentinel)) {
        ARCH_CPU_PAUSE();
    }
    __x86_64_TSC_Frequency = (EndCount - StartCount) * 1000UL /
                             TSC_CALIBRATE_TIME_MILLS;
    KDEBUG("tsc has %u hz", __x86_64_TSC_Frequency);
    ARCH_CPU_DISABLE_INTERRUPTS();
}
