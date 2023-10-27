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

#include <kmalloc.h>
#include <kmem.h>
#include <limine.h>
#include <stdbool.h>
#include <stdint.h>

#include "External/flanterm/backends/fb.h"
#include "External/flanterm/flanterm.h"
#include "Hardware/KernelDrivers/Display/vbe.h"

static struct flanterm_context *terminal;

void *memcpy(void *__dest, const void *__src, size_t __len) {
  kmemcpy(__src, __dest, __len);
  return __dest;
}

void *memset(void *__dest, int __ch, size_t __n) {
  int *_intdst = (int *)(__dest);

  for (size_t _offset = 0; _offset < __n; _offset++) {
    *_intdst = __ch;
    _offset += 1;
  }

  return __dest;
}

void kernel_terminal_initialize() {
  struct limine_framebuffer *_fb;
  kernel_hw_kdrivers_vbe_get(&_fb);
  terminal = flanterm_fb_simple_init(
      _fb->address, _fb->width, _fb->height, _fb->pitch);
  // Temporary test
  flanterm_write(terminal, "Hello, world", 12);
}
