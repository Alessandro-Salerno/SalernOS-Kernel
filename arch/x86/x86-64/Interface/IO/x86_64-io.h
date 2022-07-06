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


#ifndef SALERNOS_CORE_x8664_io
#define SALERNOS_CORE_x8664_io

    #include <kerntypes.h>


    /********************************************************************
    RET TYPE        FUNCTION NAME       FUNCTION ARGUMENTS
    ********************************************************************/
    void            x8664_io_out        (uint16_t __port, uint8_t __val);
    void            x8664_io_out_wait   (uint16_t __port, uint8_t __val);
    uint8_t         x8664_io_in         (uint16_t __port);
    uint8_t         x8664_io_in_wait    (int16_t __port);
    void            x8664_io_wait       ();

#endif
