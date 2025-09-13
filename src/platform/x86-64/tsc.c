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
#include <kernel/platform/x86-64/tsc.h>
#include <lib/util.h>
#include <stdbool.h>

#define TSC_REQUIRED_LEAF 0x80000007
#define TSC_CPUID_AVAIL   (1 << 8)

static bool tsc_probe(void) {
    uint32_t max_leaf = X86_64_CPUID_EXT_MAX_LEAF();
    if (max_leaf < TSC_REQUIRED_LEAF) {
        return false;
    }

    x86_64_cpuid_t cpuid;
    X86_64_CPUID(&cpuid, TSC_REQUIRED_LEAF);
    return TSC_CPUID_AVAIL & cpuid.edx;
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
    // TODO: implement tsc calibration
}
