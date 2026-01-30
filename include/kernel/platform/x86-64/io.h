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

#define X86_64_IO_OUTB(port, data) __hdr_x86_64_io_outb(port, data)
#define X86_64_IO_INB(port)        __hdr_x86_64_io_inb(port)
#define X86_64_IO_OUTW(port, data) __hdr_x86_64_io_outw(port, data)
#define X86_64_IO_INW(port)        __hdr_x86_64_io_inw(port)
#define X86_64_IO_OUTD(port, data) __hdr_x86_64_io_outd(port, data)
#define X86_64_IO_IND(port)        __hdr_x86_64_io_ind(port)

static inline void __hdr_x86_64_io_outb(uint16_t port, uint8_t data) {
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

static inline uint8_t __hdr_x86_64_io_inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void __hdr_x86_64_io_outw(uint16_t port, uint16_t data) {
    asm volatile("out %%ax, %%dx" : : "a"(data), "d"(port));
}

static inline uint16_t __hdr_x86_64_io_inw(uint16_t port) {
    uint16_t ret;
    asm volatile("in %%dx, %%ax" : "=a"(ret) : "d"(port));
    return ret;
}

static inline void __hdr_x86_64_io_outd(uint16_t port, uint32_t data) {
    asm volatile("out %%eax, %%dx" : : "a"(data), "d"(port));
}

static inline uint32_t __hdr_x86_64_io_ind(uint16_t port) {
    uint32_t ret;
    asm volatile("in %%dx, %%eax" : "=a"(ret) : "d"(port));
    return ret;
}
