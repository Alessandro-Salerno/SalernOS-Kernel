/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
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

#include <stdint.h>

#define X86_64_CPUID_WITH_ECX(outptr, exc, leaf) \
    __hdr_x86_64_cpuid_with_ecx(outptr, ecx, leaf)
#define X86_64_CPUID(outptr, leaf)   __hdr_x86_64_cpuid_with_ecx(outptr, leaf, 0)
#define X86_64_CPUID_EXT_MAX_LEAF()  __hdr_x86_64_cpuid_ext_max_leaf()
#define X86_64_CPUID_BASE_MAX_LEAF() __hdr_x86_64_cpuid_base_max_leaf()

#define X86_64_CPUID_LEAF_1_EDX_TSC (1 << 4)
#define X86_64_CPUID_LEAF_1_EDX_HTT (1 << 28)

typedef struct x86_64_cpuid {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
} x86_64_cpuid_t;

// TAKEn: Mathewnd/Astral
static inline void
__hdr_x86_64_cpuid_with_ecx(x86_64_cpuid_t *out, uint32_t leaf, uint32_t ecx) {
    asm volatile(
        "cpuid"
        : "=a"(out->eax), "=b"(out->ebx), "=c"(out->ecx), "=d"(out->edx)
        : "a"(leaf), "c"(ecx));
}

static inline uint32_t __hdr_x86_64_cpuid_ext_max_leaf(void) {
    x86_64_cpuid_t regs;
    X86_64_CPUID(&regs, 0x80000000);
    return regs.eax;
}

static inline uint32_t __hdr_x86_64_cpuid_base_max_leaf(void) {
    x86_64_cpuid_t regs;
    X86_64_CPUID(&regs, 0);
    return regs.eax;
}
