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


#ifndef SALERNOS_X8664_TIME_PIT
#define SALERNOS_X8664_TIME_PIT

    #include <kerntypes.h>

    #define PIT_BASE_FREQ 1193182
    #define PIT_PORT      0x40


    /*******************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    *******************************************************************/
    double          x8664_time_pit_uptime_get    ();
    void            x8664_time_pit_sleep         (uint64_t __mills);
    uint64_t        x8664_time_pit_frequency_get ();
    void            x8664_time_pit_frequency_set (uint64_t __freq);
    void            x8664_time_pit_handler_set   (void (*__handler)());
    void            x8664_time_pit_tick          ();

#endif
