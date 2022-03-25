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
#include <kmem.h>


void kernel_kutils_kdd_setup(boot_t __bootinfo) {
    kernel_kdd_fbo_bind(__bootinfo._Framebuffer);

    kernel_text_initialize(
        FGCOLOR, BGCOLOR,
        0, 0, __bootinfo._Font
    );
}

void kernel_kutils_gdt_setup() {
    gdtdesc_t _gdt_desc = (gdtdesc_t) {
        ._Size   = sizeof(gdt_t) - 1,
        ._Offset = (uint64_t)(&gdt)
    };

    kloginfo("Initializing GDT...");
    kernel_gdt_load(&_gdt_desc);
}

void kernel_kutils_mem_setup(boot_t __bootinfo) {
    uint64_t _mem_size;
    void*    _fbbase = __bootinfo._Framebuffer._BaseAddress;
    size_t   _fbsize = __bootinfo._Framebuffer._BufferSize + 4096;
    
    kloginfo("Initializing EFI Memory Map...");
    kernel_mmap_initialize(__bootinfo._Memory);
    kernel_mmap_info_get(&_mem_size, NULL, NULL, NULL);

    kernel_panic_assert(_mem_size > 64000000, "Insufficient System Memory!");

    kloginfo("Initializing Kernel Page Frame Allocator...");
    kernel_pgfa_initialize();

    kloginfo("Locking Kernel Pages...");
    uint64_t _kernel_size  = (uint64_t)(&_KERNEL_END) - (uint64_t)(&_KERNEL_START);
    uint64_t _kernel_pages = (uint64_t)(_kernel_size) / 4096 + 1;
    kernel_pgfa_lock(&_KERNEL_START, _kernel_pages);

    kloginfo("Initializing Page Table Manager...");
    pgtable_t* _lvl4 = (pgtable_t*)(kernel_pgfa_page_new());
    kmemset(_lvl4, 4096, 0);

    pgtm_t _page_table_manager = kernel_paging_initialize(_lvl4);

    kernel_paging_address_mapn((void*)(0), _mem_size);
    kernel_paging_address_mapn((void*)(_fbbase), _fbsize);

    asm ("mov %0, %%cr3" : : "r" (_lvl4)); 
    
    kloginfo("Locking Font and Framebuffer Pages...");
    kernel_pgfa_reserve(_fbbase, _fbsize / 4096 + 1);
    kernel_pgfa_reserve(__bootinfo._Font->_Buffer, (__bootinfo._Font->_Header->_CharSize * 256) / 4096);

    kloginfo("Initializing Heap...");
    kernel_heap_initialize((void*)(0x100000000000), 0x10);
}

void kernel_kutils_int_setup() {
    kloginfo("Initializing Interrupts...");
    kernel_idt_initialize();
    kernel_interrupts_pic_remap();
}

acpiinfo_t kernel_kutils_rsd_setup(boot_t __bootinfo) {
    kloginfo("Initializing ACPI...");

    sdthdr_t* _xsdt  = (sdthdr_t*)(__bootinfo._RSDP->_XSDTAddress);
    mcfghdr_t* _mcfg = (mcfghdr_t*)(kernel_hw_acpi_table_find(_xsdt, "MCFG"));

    kernel_panic_assert(kmemcmp(_mcfg->_SDTHeader._Signature, "MCFG", 4), "Invalid MCFG Table");

    return (acpiinfo_t) {
        ._XSDT = _xsdt,
        ._MCFG = _mcfg
    };
}
