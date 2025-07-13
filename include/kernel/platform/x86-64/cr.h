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

#define X86_64_CR_R0()    __hdr_x86_64_cr_r0()
#define X86_64_CR_R2()    __hdr_x86_64_cr_r2()
#define X86_64_CR_R4()    __hdr_x86_64_cr_r4()
#define X86_64_CR_W0(val) __hdr_x86_64_cr_w0(val)
#define X86_64_CR_W4(val) __hdr_x86_64_cr_w4(val)

static inline uint64_t __hdr_x86_64_cr_r0(void) {
    uint64_t val;
    asm volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

static inline uint64_t __hdr_x86_64_cr_r2(void) {
    uint64_t val;
    asm volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

static inline uint64_t __hdr_x86_64_cr_r4(void) {
    uint64_t val;
    asm volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

static inline void __hdr_x86_64_cr_w0(uint64_t val) {
    asm volatile("mov %0, %%cr0" : : "r"(val));
}

static inline void __hdr_x86_64_cr_w4(uint64_t val) {
    asm volatile("mov %0, %%cr4" : : "r"(val));
}
