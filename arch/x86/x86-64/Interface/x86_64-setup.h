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


#ifndef SALERNOS_X8664_SETUP
#define SALERNOS_X8664_SETUP

    #include <kerntypes.h>

    // Macros and constants
    #define FGCOLOR 0xffffffff
    #define BGCOLOR 0


    extern uint64_t _KERNEL_START,
                    _KERNEL_END;


    /*****************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    *****************************************************************/
    void            x8664_setup_gdt_setup       ();
    void            x8664_setup_sc_setup        ();
    void            x8664_setup_int_setup       ();
    void            x8664_setup_time_setup      ();

#endif
