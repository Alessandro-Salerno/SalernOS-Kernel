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


#ifndef SALERNOS_CORE_KERNEL_UTILITIES
#define SALERNOS_CORE_KERNEL_UTILITIES

    #include <kerntypes.h>

    // Macros and constants
    #define FGCOLOR kernel_kdd_pxcolor_translate(255, 255, 255, 255)
    #define BGCOLOR kernel_kdd_pxcolor_translate(0, 0, 0, 255)


    typedef struct BootInfo {
        // Standard Information
        uint64_t      _Reserved;

        // Bootloader Information
        const char*   _BootloaderName;
        const char*   _BootlaoderAuthor;
        const char*   _BootloaderVersion;
        const char*   _BootloaderCopyright;

        // OS Information
        framebuffer_t _Framebuffer;
        bmpfont_t     _Font;
        void*         _OSSpecific;
        void*         _Extensions;

        // Hardware
        meminfo_t     _Memory;
        rsdp_t*       _RSDP;
    } boot_t;

    extern uint64_t _KERNEL_START,
                    _KERNEL_END;

    /*****************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    *****************************************************************/
    void            kernel_kutils_kdd_setup       (boot_t __bootinfo);
    void            kernel_kutils_gdt_setup       ();
    void            kernel_kutils_mem_setup       (boot_t __bootinfo);
    void            kernel_kutils_int_setup       ();
    acpiinfo_t      kernel_kutils_rsd_setup       (boot_t __bootinfo);

#endif
