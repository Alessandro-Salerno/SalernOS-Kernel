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

#include "External/image.h"
#include "External/term.h"
#include "External/unifont.h"

static struct term_t terminal;

static volatile struct limine_framebuffer_request tmpFramebufferRequest = {
    .id       = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

static void __termbind_callback__(struct term_t *__term,
                                  uint64_t       __unused0,
                                  uint64_t       __unused1,
                                  uint64_t       __unused2,
                                  uint64_t       __unused3) {
  // Handle callback
}

// Implementations for functions used by the terminal
void *alloc_mem(size_t __size) {
  void *_buf = kmalloc(__size);
  kmemset(_buf, __size, 0);
  return _buf;
}

void free_mem(void *__buf, size_t __unused) {
  kfree(__buf);
}

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
  term_init(&terminal, __termbind_callback__, false, 4);

  struct background_t _background = {NULL, STRETCHED, DEFAULT_BACKGROUND};

  struct font_t _font = {
      (uintptr_t)unifont,
      8,
      16,
      1,
      1,
      1,
  };

  struct framebuffer_t _framebuffer = {
      (uintptr_t)tmpFramebufferRequest.response->framebuffers[0]->address,
      tmpFramebufferRequest.response->framebuffers[0]->width,
      tmpFramebufferRequest.response->framebuffers[0]->height,
      tmpFramebufferRequest.response->framebuffers[0]->pitch};

  struct style_t _style = {
      DEFAULT_ANSI_COLOURS,        // Default terminal palette
      DEFAULT_ANSI_BRIGHT_COLOURS, // Default terminal bright palette
      DEFAULT_BACKGROUND,          // Background colour
      DEFAULT_FOREGROUND,          // Foreground colour
      DEFAULT_MARGIN,              // Terminal margin
      DEFAULT_MARGIN_GRADIENT      // Terminal margin gradient
  };

  term_vbe(&terminal, _framebuffer, _font, _style, _background);
  term_write(&terminal, "Setup", 5);
}
