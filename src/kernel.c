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


#include <kerninc.h>
#include <kdebug.h>


void kernel_main(boot_t* __bootinfo) {
    uint64_t _mem_size,
             _usable_mem,
             _free_mem,
             _used_mem,
             _unusable_mem;

    uint64_t _kernel_size  = (uint64_t)(&_KERNEL_END) - (uint64_t)(&_KERNEL_START);
    uint64_t _kernel_pages = (uint64_t)(_kernel_size) / 4096 + 1;

    kernel_kutils_kdd_setup(__bootinfo);
    kernel_kutils_gdt_setup();
    kernel_kutils_mem_setup(__bootinfo);
    kernel_kutils_int_setup();
    kernel_kutils_time_setup();

    acpiinfo_t _acpi = kernel_kutils_rsd_setup(__bootinfo);
    
    kernel_mmap_info_get(&_mem_size, &_usable_mem, NULL, NULL);

    kernel_text_reinitialize(
        FGCOLOR, BGCOLOR,
        0, 0
    );

    kprintf("Copyright 2021 - 2022 Alessandro Salerno. All rights reserved.\n");
    kprintf("%s %s\n", __bootinfo->_BootloaderName, __bootinfo->_BootloaderVersion);
    kprintf("SalernOS Kernel DEV (Rome)\n\n");

    kprintf("Kernel Base: %u\n", (uint64_t)(&_KERNEL_START));
    kprintf("Kernel End: %u\n", (uint64_t)(&_KERNEL_END));
    kprintf("Kernel Size: %u bytes (%u pages)\n\n", _kernel_size, _kernel_pages);

    kprintf("Framebuffer Resolution: %u x %u\n", __bootinfo->_Framebuffer._Width, __bootinfo->_Framebuffer._Height);
    kprintf("Framebuffer Base: %u\n",            __bootinfo->_Framebuffer._BaseAddress);
    kprintf("Framebuffer Size: %u bytes\n\n",    __bootinfo->_Framebuffer._BufferSize);

    kernel_pgfa_info_get(&_free_mem, &_used_mem, &_unusable_mem);
    kprintf("System Memory: %u bytes\n",   _mem_size);
    kprintf("Usable Memory: %u bytes\n",   _usable_mem);
    kprintf("Free Memory: %u bytes\n",     _free_mem);
    kprintf("Used Memory: %u bytes\n",     _used_mem);
    kprintf("Reserved Memory: %u bytes\n\n", _unusable_mem);

    kprintf("RSDP Address: %u\n",   (uint64_t)(__bootinfo->_RSDP));
    kprintf("MCFG Address: %u\n", (uint64_t)(_acpi._MCFG));

    klogok("Kernel Ready!");
    kprintf("\n\n");

    // What follows is just debug code
    // This will be removed in a later version
    uint8_t _kcode = 0, _nkcode = 0, _ascii = 0;
    while (TRUE) {
        kernel_io_keyboard_keys_handle();
        kernel_io_keyboard_keys_get(&_nkcode, &_ascii);
        
        if (_nkcode == _kcode) continue;
        _kcode = _nkcode;

        switch (_kcode) {
            case KEY_ENTER     : kprintf("\n"); break;
            case KEY_SPACE     : kprintf(" ");  break;
            
            default:
                kprintf("%c", _ascii);
                break;
        }
    }
}
