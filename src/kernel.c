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
#include <kstdio.h>
#include <limine.h>

#include "dev/kernel-drivers/fb/fb.h"
#include "mm/pmm.h"
#include "sched/lock.h"
#include "sys/gdt/gdt.h"
#include "termbind.h"

static volatile struct limine_framebuffer_request framebufferRequest = {
    .id       = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

static volatile struct limine_hhdm_request hhdmRequest = {
    .id       = LIMINE_HHDM_REQUEST,
    .revision = 0};

void kernel_main() {
  boot_t *__bootinfo = NULL;
  hw_kdrivers_fb_initialize(framebufferRequest.response);
  terminal_initialize();

  kloginfo("Testing lock acquire...");
  lock_t _test_lock = SCHED_LOCK_NEW();
  sched_lock_acquire(&_test_lock);
  klogok("Lock acquired");
  kloginfo("Testing lock release...");
  sched_lock_release(&_test_lock);
  klogok("Lock released");

  sys_gdt_initialize();
  pmm_initialize(hhdmRequest.response);

  kprintf(
      "\n\nCopyright 2021 - 2023 Alessandro Salerno. All rights reserved.\n");
  kprintf("SalernOS Kernel 0.0.7\n");

  while (true)
    asm("hlt");
  uint64_t _mem_size, _usable_mem, _free_mem, _used_mem, _unusable_mem;

  uint64_t _size  = (uint64_t)(&_KERNEL_END) - (uint64_t)(&_KERNEL_START);
  uint64_t _pages = (uint64_t)(_size) / 4096 + 1;

  kutils_kdd_setup(__bootinfo);
  kutils_gdt_setup();
  kutils_mem_setup(__bootinfo);
  kutils_sc_setup();
  kutils_int_setup();
  kutils_time_setup();

  acpiinfo_t _acpi = kutils_rsd_setup(__bootinfo);

  mmap_info_get(&_mem_size, &_usable_mem, NULL, NULL);
  kprintf(
      "%s %s\n", __bootinfo->_BootloaderName, __bootinfo->_BootloaderVersion);

  kprintf("Kernel Base: %u\n", (uint64_t)(&_KERNEL_START));
  kprintf("Kernel End: %u\n", (uint64_t)(&_KERNEL_END));
  kprintf("Kernel Size: %u bytes (%u pages)\n\n", _size, _pages);

  kprintf("Framebuffer Resolution: %u x %u\n",
          __bootinfo->_Framebuffer._Width,
          __bootinfo->_Framebuffer._Height);
  kprintf("Framebuffer Base: %u\n", __bootinfo->_Framebuffer._BaseAddress);
  kprintf("Framebuffer Size: %u bytes\n\n",
          __bootinfo->_Framebuffer._BufferSize);

  pgfa_info_get(&_free_mem, &_used_mem, &_unusable_mem);
  kprintf("System Memory: %u bytes\n", _mem_size);
  kprintf("Usable Memory: %u bytes\n", _usable_mem);
  kprintf("Free Memory: %u bytes\n", _free_mem);
  kprintf("Used Memory: %u bytes\n", _used_mem);
  kprintf("Reserved Memory: %u bytes\n\n", _unusable_mem);

  kprintf("RSDP Address: %u\n", (uint64_t)(__bootinfo->_RSDP));
  kprintf("MCFG Address: %u\n", (uint64_t)(_acpi._MCFG));

  klogok("Kernel Ready!");
  kprintf("\n\n");

  while (TRUE) {
    asm volatile("hlt");
  }
}
