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


#ifndef SALERNOS_CORE_KERNEL_INTERRUPT_HANDLERS
#define SALERNOS_CORE_KERNEL_INTERRUPT_HANDLERS

    #include <kerntypes.h>

    #define ISR __attribute__((interrupt))


    struct InterruptFrame;
    typedef struct InterruptFrame intframe_t;


    /*********************************************************************************
         RET TYPE        FUNCTION NAME                          FUNCTION ARGUMENTS
    *********************************************************************************/
    ISR  void            kernel_interrupt_handlers_pgfault      (intframe_t* __frame);
    ISR  void            kernel_interrupt_handlers_dfault       (intframe_t* __frame);
    ISR  void            kernel_interrupt_handlers_gpfault      (intframe_t* __frame);
    ISR  void            kernel_interrupt_handlers_tick         (intframe_t* __frame);

#endif
