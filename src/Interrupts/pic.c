/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2022 Alessandro Salerno

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**********************************************************************/


#include "Interrupts/pic.h"
#include "IO/io.h"


void kernel_interrupts_pic_remap() {
    asm volatile ("cli");

    uint8_t _pic1 = kernel_io_in_wait(PIC1_DATA),
            _pic2 = kernel_io_in_wait(PIC2_DATA);

    kernel_io_out_wait(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    kernel_io_out_wait(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    kernel_io_out_wait(PIC1_DATA, 0x20);
    kernel_io_out_wait(PIC2_DATA, 0x28);
    kernel_io_out_wait(PIC1_DATA, 0x04);
    kernel_io_out_wait(PIC2_DATA, 0x02);
    kernel_io_out_wait(PIC1_DATA, ICW4_8086);
    kernel_io_out_wait(PIC2_DATA, ICW4_8086);

    kernel_io_out(PIC1_DATA, _pic1);
    kernel_io_out(PIC2_DATA, _pic2);

    kernel_io_out(PIC1_DATA, 0b11111101);
    kernel_io_out(PIC2_DATA, 0b11111111);
    asm volatile ("sti");
}

void kernel_interrupts_pic_master_end() {
    kernel_io_out(PIC1_COMMAND, PIC_EOI);
}

void kernel_interrupts_pic_slave_end() {
    kernel_io_out(PIC2_COMMAND, PIC_EOI);
    kernel_io_out(PIC1_COMMAND, PIC_EOI);
}
