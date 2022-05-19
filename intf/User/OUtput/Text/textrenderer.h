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


#ifndef SALERNOS_CORE_arch_boot_text
#define SALERNOS_CORE_arch_boot_text

    #include <kerntypes.h>

    #define CURSOR_CHARACTER '_'


    /***********************************************************************************************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    ***********************************************************************************************************************************************/
    void            arch_boot_text_blitch         (char __char);
    void            arch_boot_text_putch          (char __char);
    void            arch_boot_text_print          (char* __str);

    void            arch_boot_text_initialize     (uint32_t __color, uint32_t __backcolor, uint32_t __xoff, uint32_t __yoff, bmpfont_t __font); 
    void            arch_boot_text_reinitialize   (uint32_t __color, uint32_t __backcolor, uint32_t __xoff, uint32_t __yoff);

    void            arch_boot_text_scroll         (uint32_t __lines);
    void            arch_boot_text_line_clear     (uint32_t __line);
    
    void            arch_boot_text_info_set       (uint32_t __color, uint32_t __backcolor, point_t __pos);
    void            arch_boot_text_info_get       (uint32_t* __color, uint32_t* __backcolor, point_t* __curpos, point_t* __lpos, bmpfont_t* __font);

#endif
