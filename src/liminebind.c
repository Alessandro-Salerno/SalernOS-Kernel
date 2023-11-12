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

#include <limine.h>

static volatile struct limine_hhdm_request hhdmRequest = {
    .id       = LIMINE_HHDM_REQUEST,
    .revision = 0};

static volatile struct limine_framebuffer_request framebufferRequest = {
    .id       = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

static volatile struct limine_memmap_request memoryMapRequest = {
    .id       = LIMINE_MEMMAP_REQUEST,
    .revision = 0};

void *liminebind_transptr(void *__addr) {
  return __addr + hhdmRequest.response->offset;
}

uint64_t liminebind_transint(uint64_t __addr) {
  return __addr + (uint64_t)hhdmRequest.response->offset;
}

struct limine_framebuffer_response *liminebind_fb_get() {
  return framebufferRequest.response;
}

struct limine_memmap_response *liminebind_mmap_get() {
  return memoryMapRequest.response;
}
