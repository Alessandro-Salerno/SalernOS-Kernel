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

#include "User/Output/Display/kdd.h"
#include "kernelpanic.h"

static framebuffer_t boundFBO;

static uint32_t *__get_pxptr__(uint64_t __x, uint64_t __y) {
  SOFTASSERT(boundFBO._BaseAddress != NULL, NULL);

  return (uint32_t *){__x * BYTES_PER_PIXEL +
                      __y * BYTES_PER_PIXEL * boundFBO._Width +
                      boundFBO._BaseAddress};
}

void kernel_kdd_fbo_bind(framebuffer_t __fbo) {
  boundFBO = __fbo;
}

void kernel_kdd_fbo_clear(uint32_t __clearcolor) {
  SOFTASSERT(boundFBO._BaseAddress != NULL, RETVOID);

  uint64_t _64bit_color = __clearcolor | ((uint64_t)(__clearcolor) << 32);
  for (uint64_t _i = 0; _i < boundFBO._BufferSize; _i += 8)
    *(uint64_t *)((uint64_t)(boundFBO._BaseAddress) + _i) = _64bit_color;
}

framebuffer_t kernel_kdd_fbo_get() {
  return boundFBO;
}

uint32_t kernel_kdd_pxcolor_translate(uint8_t __r,
                                      uint8_t __g,
                                      uint8_t __b,
                                      uint8_t __a) {
  return __a << 24 | __r << 16 | __g << 8 | __b;
}

uint32_t kernel_kdd_pxcolor_get(uint32_t __x, uint32_t __y) {
  return *(__get_pxptr__(__x, __y));
}

void kernel_kdd_pxcolor_set(uint32_t __x, uint32_t __y, uint32_t __color) {
  *(__get_pxptr__(__x, __y)) = __color;
}
