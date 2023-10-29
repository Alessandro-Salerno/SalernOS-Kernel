/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2023 Alessandro Salerno

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

#include "sys/legacy-io/io.h"

void kernel_io_out(uint16_t __port, uint8_t __val) {
  asm volatile("outb %0, %1" : : "a"(__val), "Nd"(__port));
}

uint8_t kernel_io_in(uint16_t __port) {
  uint8_t _retval;
  asm volatile("inb %1, %0" : "=a"(_retval) : "Nd"(__port));

  return _retval;
}

void kernel_io_wait() {
  asm volatile("outb %%al, $0x80" : : "a"(0));
}

void kernel_io_out_wait(uint16_t __port, uint8_t __val) {
  kernel_io_out(__port, __val);
  kernel_io_wait();
}

uint8_t kernel_io_in_wait(int16_t __port) {
  uint8_t _ret = kernel_io_in(__port);
  kernel_io_wait();

  return _ret;
}
