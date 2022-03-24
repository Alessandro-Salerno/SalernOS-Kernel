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


#include "Time/PIT/pit.h"
#include "kernelpanic.h"
#include "IO/io.h"

#define DIV_MIN 100


static double timeSinceBoot;
static uint16_t divisor;
static void (*tickHandler)();


static void __set_divisor__(uint16_t __div) {
    if (__div < DIV_MIN) __div = DIV_MIN;
    divisor = __div;

    kernel_io_out_wait(PIT_PORT, (uint8_t)(divisor & 0xff));
    kernel_io_out(PIT_PORT, (uint8_t)(divisor & 0xff00) >> 8);
}

double kernel_time_pit_uptime_get() {
    return timeSinceBoot;
}

void kernel_time_pit_sleep(uint64_t __mills) {
    double _start = timeSinceBoot;
    while (timeSinceBoot < _start + __mills * 100)
        asm ("hlt");
}

uint64_t kernel_time_pit_frequency_get() {
    return PIT_BASE_FREQ / divisor;
}

void kernel_time_pit_frequency_set(uint64_t __freq) {
    __set_divisor__(PIT_BASE_FREQ / __freq);
}

void kernel_time_pit_handler_set(void (*__handler)()) {
    tickHandler = __handler;
}

void kernel_time_pit_tick() {
    timeSinceBoot += 1 / (double)(kernel_time_pit_frequency_get());

    SOFTASSERT(tickHandler != NULL, RETVOID);
    tickHandler();
}
