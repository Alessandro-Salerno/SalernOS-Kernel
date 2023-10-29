/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2023 Alessandro Salerno

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

#include <kdebug.h>
#include <kerninc.h>
#include <kmem.h>

void kutils_kdd_setup(boot_t *__bootinfo) {
  // kdd_fbo_bind(__bootinfo->_Framebuffer);

  // text_initialize(FGCOLOR, BGCOLOR, 0, 0, __bootinfo->_Font);
}

void kutils_gdt_setup() {
  gdtdesc_t _gdt_desc =
      (gdtdesc_t){._Size = sizeof(gdt) - 1, ._Offset = (uint64_t)(&gdt)};

  kloginfo("Initializing TSS...");
  kmemset(&tss, sizeof(tss_t), 0);
  uint64_t _tss_base = (uint64_t)(&tss);

  gdt._TSSLowSegment._LimitZero  = sizeof(tss_t);
  gdt._TSSLowSegment._BaseZero   = _tss_base & 0xffff;
  gdt._TSSLowSegment._BaseOne    = (_tss_base >> 16) & 0x00ff;
  gdt._TSSLowSegment._BaseTwo    = (_tss_base >> 24) & 0x00ff;
  gdt._TSSHighSegment._LimitZero = (_tss_base >> 32) & 0xffff;
  gdt._TSSHighSegment._BaseZero  = (_tss_base >> 48) & 0xffff;

  kloginfo("Initializing GDT...");
  gdt_load(&_gdt_desc);
}

void kutils_mem_setup(boot_t *__bootinfo) {
  uint64_t _mem_size;
  void    *_fbbase = __bootinfo->_Framebuffer._BaseAddress;
  size_t   _fbsize = __bootinfo->_Framebuffer._BufferSize + 4096;

  kloginfo("Initializing EFI Memory Map...");
  mmap_initialize(__bootinfo->_Memory);
  mmap_info_get(&_mem_size, NULL, NULL, NULL);

  panic_assert(_mem_size > 64000000, "Insufficient System Memory!");

  kloginfo("Initializing Kernel Page Frame Allocator...");
  pgfa_initialize();

  kloginfo("Locking Kernel Pages...");
  uint64_t _size  = (uint64_t)(&_KERNEL_END) - (uint64_t)(&_KERNEL_START);
  uint64_t _pages = (uint64_t)(_size) / 4096 + 1;
  pgfa_lock(&_KERNEL_START, _pages);

  kloginfo("Initializing Page Table Manager...");
  pgtable_t *_lvl4 = (pgtable_t *)(pgfa_page_new());
  kmemset(_lvl4, 4096, 0);

  paging_initialize(_lvl4);

  paging_address_mapn((void *)(0), _mem_size);
  paging_address_mapn((void *)(_fbbase), _fbsize);

  paging_laod(_lvl4);

  kloginfo("Locking Font and Framebuffer Pages...");
  pgfa_reserve(_fbbase, _fbsize / 4096 + 1);
  pgfa_reserve(__bootinfo->_Font._Buffer,
               (__bootinfo->_Font._Header._CharSize * 256) / 4096);

  kloginfo("Initializing Heap...");
  heap_initialize((void *)(0x100000000000), 0x10);
}

void kutils_sc_setup() {
  kloginfo("Enabling SCE...");
  syscall_enable();
}

void kutils_int_setup() {
  kloginfo("Initializing Interrupts...");
  idt_initialize();
  interrupts_pic_remap();
}

acpiinfo_t kutils_rsd_setup(boot_t *__bootinfo) {
  kloginfo("Initializing ACPI...");

  sdthdr_t  *_xsdt = (sdthdr_t *)(__bootinfo->_RSDP->_XSDTAddress);
  mcfghdr_t *_mcfg = (mcfghdr_t *)(hw_acpi_table_find(_xsdt, "MCFG"));

  panic_assert(kmemcmp(_mcfg->_SDTHeader._Signature, "MCFG", 4),
               "Invalid MCFG Table");

  kloginfo("Initializing PCI...");
  hw_pci_enumerate(_mcfg);

  return (acpiinfo_t){._XSDT = _xsdt, ._MCFG = _mcfg};
}

void kutils_time_setup() {
  kloginfo("Initializing PIT...");
  time_pit_frequency_set(1);
}
