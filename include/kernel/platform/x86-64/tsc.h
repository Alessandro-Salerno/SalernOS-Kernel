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

#define X86_64_TSC_READ() __hdr_x86_64_tsc_read()
#define X86_64_TSC_GET_NS() \
    __hdr_x86_64_tsc_get_ns(ARCH_CPU_GET()->tsc_reverse_mult)
#define X86_64_TSC_VAL_TO_NS(reverse_mult, val) \
    __hdr_x86_64_tsc_val_to_ns(reverse_mult, val)

void x86_64_tsc_boot(void);
void x86_64_tsc_bsp_init(void);
void x86_64_tsc_init(void);

static inline uint64_t __hdr_x86_64_tsc_read(void) {
    uint32_t high;
    uint32_t low;
    asm volatile("rdtscp" : "=a"(low), "=d"(high) : : "rcx");
    return ((uint64_t)high << 32) | low;
}

static inline uintmax_t __hdr_x86_64_tsc_val_to_ns(uint64_t  tsc_reverse_mult,
                                                   uintmax_t val) {
    return (uint64_t)(((__uint128_t)val * tsc_reverse_mult) >> 32);
}

static inline uintmax_t __hdr_x86_64_tsc_get_ns(uint64_t tsc_reverse_mult) {
    extern uint64_t __x86_64_TSC_BootTime;
    return X86_64_TSC_VAL_TO_NS(tsc_reverse_mult, X86_64_TSC_READ()) -
           __x86_64_TSC_BootTime;
}
