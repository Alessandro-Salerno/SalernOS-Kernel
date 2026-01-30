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

#define X86_64_MSR_EFER         0xC0000080
#define X86_64_MSR_STAR         0xC0000081
#define X86_64_MSR_LSTAR        0xC0000082
#define X86_64_MSR_CSTAR        0xC0000083
#define X86_64_MSR_FMASK        0xC0000084
#define X86_64_MSR_FSBASE       0xC0000100
#define X86_64_MSR_GSBASE       0xC0000101
#define X86_64_MSR_KERNELGSBASE 0xC0000102

#define X86_64_MSR_WRITE(reg, val) __hdr_x86_64_msr_write(reg, val)
#define X86_64_MSR_READ(reg)       __hdr_x86_64_msr_read(reg)

static inline uint32_t __hdr_x86_64_msr_read(uint32_t reg) {
    uint64_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(reg));
    return (high << 32) | low;
}

static inline void __hdr_x86_64_msr_write(uint32_t reg, uint64_t val) {
    uint32_t low  = val & 0xFFFFFFFF;
    uint32_t high = (val >> 32) & 0xFFFFFFFF;
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(reg));
}
