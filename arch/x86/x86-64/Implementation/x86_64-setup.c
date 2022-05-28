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


#include "Syscall/x86_64-sce.h"
#include "x86_64-setup.h"
#include <kerninc.h>
#include <kdebug.h>
#include <kmem.h>


void x8664_setup_gdt_setup() {
    gdtdesc_t _gdt_desc = (gdtdesc_t) {
        ._Size   = sizeof(gdt) - 1,
        ._Offset = (uint64_t)(&gdt)
    };

    kloginfo("Initializing TSS...");
    kmemset(&tss, sizeof(tss_t), 0);
    uint64_t _tss_base = (uint64_t)(&tss);
    
    gdt._TSSLowSegment._LimitZero  = sizeof(tss_t);
    gdt._TSSLowSegment._BaseZero   = _tss_base         & 0xffff;
    gdt._TSSLowSegment._BaseOne    = (_tss_base >> 16) & 0x00ff;
    gdt._TSSLowSegment._BaseTwo    = (_tss_base >> 24) & 0x00ff;
    gdt._TSSHighSegment._LimitZero = (_tss_base >> 32) & 0xffff;
    gdt._TSSHighSegment._BaseZero  = (_tss_base >> 48) & 0xffff;

    kloginfo("Initializing GDT...");
    kernel_gdt_load(&_gdt_desc);
}


void x8664_setup_sc_setup() {
    kloginfo("Enabling SCE...");
    x8664_syscall_enable();
}

void x8664_setup_int_setup() {
    kloginfo("Initializing Interrupts...");
    kernel_idt_initialize();
    kernel_interrupts_pic_remap();
}

void x8664_setup_time_setup() {
    kloginfo("Initializing PIT...");
    kernel_time_pit_frequency_set(1);
}
