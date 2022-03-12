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


    typedef struct Framebuffer {
        void*    _BaseAddress;
        size_t   _BufferSize;
        uint32_t _Width;
        uint32_t _Height;
        uint32_t _PixelsPerScanLine;
        uint8_t  _BytesPerPixel;
    } framebuffer_t;

    typedef struct BitmapFontHeader {
        uint8_t _Magic[2];
        uint8_t _Mode;
        uint8_t _CharSize;
    } bmpfonthdr_t;

    typedef struct bmpfont_t {
        bmpfonthdr_t* _Header;
        void*         _Buffer;
    } bmpfont_t;

    typedef struct Point {
        uint32_t x;
        uint32_t y;
    } point_t;


    /****************************************************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    ****************************************************************************************************/
    void            kernel_kdd_fbo_bind           (framebuffer_t* __fbo);
    void            kernel_kdd_fbo_clear          (uint32_t __clearcolor);
    framebuffer_t*  kernel_kdd_fbo_get            ();
    
    uint32_t        kernel_kdd_pxcolor_translate  (uint8_t  __r, uint8_t  __g, uint8_t __b, uint8_t __a);
    uint32_t        kernel_kdd_pxcolor_get        (uint32_t __x, uint32_t __y);
    void            kernel_kdd_pxcolor_set        (uint32_t __x, uint32_t __y, uint32_t __color);

#endif
