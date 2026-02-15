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

#define X86_64_TSC_READ()               __hdr_x86_64_tsc_read()
#define X86_64_TSC_GET_NS()             __hdr_x86_64_tsc_get_ns(ARCH_CPU_GET()->tsc_freq)
#define X86_64_TSC_VAL_TO_NS(freq, val) __hdr_x86_64_tsc_val_to_ns(freq, val)

void x86_64_tsc_boot(void);
void x86_64_tsc_bsp_init(void);
void x86_64_tsc_init(void);

static inline uint64_t __hdr_x86_64_tsc_read(void) {
    uint32_t high;
    uint32_t low;
    asm volatile("cpuid; rdtsc;" : "=a"(low), "=d"(high) : : "rbx", "rcx");
    return ((uint64_t)high << 32) | low;
}

static inline uintmax_t __hdr_x86_64_tsc_val_to_ns(uint64_t  tsc_freq,
                                                   uintmax_t val) {
    if (0 == tsc_freq) {
        return 0;
    }

    uint64_t secs       = val / tsc_freq;
    uint64_t secs_to_ns = secs * 1000UL * 1000UL * 1000UL;

    // Given the high precision of the TSC, it's unlikelly this function will be
    // called exactly on a second boundary. As such, we have to compute the
    // remainder too.
    // However, here we ask ourselves the question: if val / freq is a lower
    // boundary, how can we compute the numebr of nanoseconds remaining. The
    // current relatively safe answer I found is to calculate the remainder in
    // ticks, and multiply that by 1000000, so that dividing by freq will return
    // the number of microseconds instead of seconds. Multiplying should be
    // pretty overflow-safe since rem < freq, and freq is unlikelly to be
    // high enought to cause uint64_t to overflow is multiplied by 1000000.
    uint64_t rem_ticks = val - secs * tsc_freq;
    uint64_t rem_micro = rem_ticks * 1000UL * 1000UL / tsc_freq;

    // And here we do the same to get the last remaining nanoseconds
    uint64_t rem_micro_ticks = rem_micro * tsc_freq / (1000UL * 1000UL);
    uint64_t rem_ticks2      = rem_ticks - rem_micro_ticks;
    uint64_t rem_nano        = rem_ticks2 * 1000UL * 1000UL * 1000UL / tsc_freq;

    return secs_to_ns + (rem_micro * 1000UL) + rem_nano;
}

static inline uintmax_t __hdr_x86_64_tsc_get_ns(uint64_t freq) {
    extern uint64_t __x86_64_TSC_BootTime;
    return X86_64_TSC_VAL_TO_NS(freq, X86_64_TSC_READ()) -
           __x86_64_TSC_BootTime;
}
