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


#ifndef SALERNOS_X8664_SBS_SETUP
#define SALERNOS_X8664_SBS_SETUP

    #include "x86_64-setup.h"
    #include <kerninc.h>
    #include <sbs.h>

    
    typedef struct SimpleBootInformationTable boot_t;


    /********************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    ********************************************************************/
    void            x8664_sbs_setup_kdd_setup       (boot_t* __bootinfo);
    void            x8664_sbs_setup_mem_setup       (boot_t* __bootinfo);
    acpiinfo_t      x8664_sbs_setup_rsd_setup       (boot_t* __bootinfo);

#endif
