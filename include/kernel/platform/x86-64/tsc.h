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

#pragma once

#include <stdint.h>

#define X86_64_TSC_READ() __hdr_x86_64_tsc_raed()

void x86_64_tsc_init(void);

static inline uint64_t __hdr_x86_64_tsc_read(void) {
    uint32_t high;
    uint32_t low;
    asm volatile("cpuid; rdtsc;" : "=a"(low), "=d"(high) : : "rbx", "rcx");
    return ((uint64_t)high << 32) | low;
}
