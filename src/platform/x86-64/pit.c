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

#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/pit.h>
#include <stdbool.h>
#include <stdint.h>

static uint16_t pit_read(void) {
    X86_64_IO_OUTB(0x43, 0);
    uint8_t low  = X86_64_IO_INB(0x40);
    uint8_t high = X86_64_IO_INB(0x40);
    return low | high << 8;
}

uint64_t x86_64_pit_wait_ticks(uint64_t ticks) {
    X86_64_IO_OUTB(0x43, 0x34);
    X86_64_IO_OUTB(0x40, 0xff);
    X86_64_IO_OUTB(0x40, 0xff);

    uint64_t elapsed_ticks = 0;
    uint16_t prev          = 0xffff - pit_read();

    while (elapsed_ticks < ticks) {
        uint16_t now   = 0xffff - pit_read();
        uint16_t delta = now - prev;
        elapsed_ticks += delta;
        prev = now;
    }

    return elapsed_ticks;
}
