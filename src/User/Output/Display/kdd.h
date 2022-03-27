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


#ifndef SALERNOS_CORE_KERNEL_DISPLAY_DRIVER
#define SALERNOS_CORE_KERNEL_DISPLAY_DRIVER

    #include <kerntypes.h>
    #include <sbs.h>

    #define BYTES_PER_PIXEL 4


    typedef struct Point {
        uint32_t x;
        uint32_t y;
    } point_t;

    typedef struct SimpleBootFramebuffer framebuffer_t;
    typedef struct SimpleBootFontHeader  bmpfonthdr_t;
    typedef struct SimpleBootFont        bmpfont_t;


    /****************************************************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    ****************************************************************************************************/
    void            kernel_kdd_fbo_bind           (framebuffer_t __fbo);
    void            kernel_kdd_fbo_clear          (uint32_t __clearcolor);
    framebuffer_t   kernel_kdd_fbo_get            ();
    
    uint32_t        kernel_kdd_pxcolor_translate  (uint8_t  __r, uint8_t  __g, uint8_t __b, uint8_t __a);
    uint32_t        kernel_kdd_pxcolor_get        (uint32_t __x, uint32_t __y);
    void            kernel_kdd_pxcolor_set        (uint32_t __x, uint32_t __y, uint32_t __color);

#endif
