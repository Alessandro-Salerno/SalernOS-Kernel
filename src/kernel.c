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
  mm_pmm_initialize(hhdmRequest.response);

  kloginfo("Testing page allocation...");
  void *_test_page = mm_pmm_alloc();
  mm_pmm_free(_test_page, 1);
  void *_test_page2 = mm_pmm_alloc();
  mm_pmm_free(_test_page2, 1);

  if (_test_page != _test_page2) {
    klogwarn("Memory leak detected! First page at %u, second at %u",
             (uint64_t)_test_page,
             (uint64_t)_test_page2);
  } else {
    klogok("Page allocation and deallocation test passed!");
  }

  kprintf(
      "\n\nCopyright 2021 - 2023 Alessandro Salerno. All rights reserved.\n");
  kprintf("SalernOS Kernel 0.0.7\n");

  while (true)
    asm volatile("hlt");
}
