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


#include "Time/PIT/x86_64-pit.h"
#include "IO/x86_64-io.h"
#include <kernelpanic.h>
#include "panicassert.h"

#define DIV_MIN 100


static volatile double   uptime  = 0;
static uint16_t          divisor = 63535;

static void (*tickHandler)();


static void __set_divisor__(uint16_t __div) {
    if (__div < DIV_MIN) __div = DIV_MIN;
    divisor = __div;

    x8664_io_out_wait(PIT_PORT, (uint8_t)(divisor & 0x00ff));
    x8664_io_out(PIT_PORT, (uint8_t)((divisor & 0xff00) >> 8));
}

double x8664_time_pit_uptime_get() {
    return uptime;
}

void x8664_time_pit_sleep(uint64_t __mills) {
    const double _START = uptime;
    while (uptime < _START + (double)(__mills) / 1000)
        asm volatile ("hlt");
}

uint64_t x8664_time_pit_frequency_get() {
    return 65535 / divisor;
}

void x8664_time_pit_frequency_set(uint64_t __freq) {
    __set_divisor__(65535 / __freq);
}

void x8664_time_pit_handler_set(void (*__handler)()) {
    tickHandler = __handler;
}

void x8664_time_pit_tick() {
    uptime += 1.0f / (double)(x8664_time_pit_frequency_get());

    SOFTASSERT(tickHandler != NULL, RETVOID);
    tickHandler();
}
