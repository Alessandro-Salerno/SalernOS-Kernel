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
#include <kstr.h>
#include <limine.h>
#include <stdbool.h>
#include <stdint.h>

#include "External/flanterm/backends/fb.h"
#include "External/flanterm/flanterm.h"
#include "Hardware/KernelDrivers/Display/vbe.h"
#include "kerntypes.h"
#include "termbind.h"

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

void kernel_terminal_putchar(char __ch) {
  terminal->raw_putchar(terminal, __ch);
}

void kernel_terminal_flush() {
  terminal->double_buffer_flush(terminal);
}

void kernel_terminal_write(const char *__str) {
  flanterm_write(terminal, __str, kstrlen(__str));
}

void kernel_terminal_fastwrite(const char *__str) {
  for (; *__str; __str++) {
    kernel_terminal_putchar(*__str);
  }
}

void kernel_terminal_info_get(uint32_t *__bg, uint32_t *__fg) {
  ARGRET(__bg, terminal->current_bg);
  ARGRET(__bg, terminal->current_primary);
}

void kernel_terminal_info_set(uint32_t __bg, uint32_t __fg) {
  terminal->set_text_bg_rgb(terminal, __bg);
  terminal->set_text_fg_rgb(terminal, __fg);
}

void kernel_terminal_initialize() {
  struct limine_framebuffer *_fb;
  kernel_hw_kdrivers_vbe_get(&_fb);
  terminal = flanterm_fb_simple_init(
      _fb->address, _fb->width, _fb->height, _fb->pitch);
  // Temporary test
  terminal->autoflush = false;
}
