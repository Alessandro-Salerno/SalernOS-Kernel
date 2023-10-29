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

#include <kmem.h>
#include <kstdio.h>
#include <limine.h>

#include "kdebug.h"
#include "kernelpanic.h"
#include "mm/bmp.h"
#include "mm/pmm.h"

static volatile struct limine_memmap_request memoryMapRequest = {
    .id       = LIMINE_MEMMAP_REQUEST,
    .revision = 0};

static volatile struct limine_memmap_response *memoryMapResponse;

static bmp_t    pageBitmap;
static uint64_t bitmapIndex;

static uint64_t memSize;
static uint64_t usablemem;
static uint64_t freeMem;
static uint64_t reservedMem;
static uint64_t usedMem;

static void __reserve_page__(void *__address, uint64_t *__rsvmemcount) {
  uint64_t _idx = (uint64_t)(__address) / 4096;

  SOFTASSERT(!(kernel_bmp_get(&pageBitmap, _idx)), RETVOID)

  kernel_bmp_set(&pageBitmap, _idx, 1);
  freeMem -= 4096;
  *__rsvmemcount += 4096;
}

static void __unreserve_page__(void *__address, uint64_t *__rsvmemcount) {
  uint64_t _idx = (uint64_t)(__address) / 4096;

  SOFTASSERT(kernel_bmp_get(&pageBitmap, _idx), RETVOID)

  kernel_bmp_set(&pageBitmap, _idx, 0);
  freeMem += 4096;
  *__rsvmemcount -= 4096;

  bitmapIndex = (bitmapIndex > _idx) ? _idx : bitmapIndex;
}

void kernel_pmm_free(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __unreserve_page__((void *)((uint64_t)(__address) + (_i * 4096)), &usedMem);
}

void kernel_pmm_lock(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __reserve_page__((void *)((uint64_t)(__address) + (_i * 4096)), &usedMem);
}

void kernel_pmm_unreserve(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __unreserve_page__((void *)((uint64_t)(__address) + (_i * 4096)),
                       &reservedMem);
}

void kernel_pmm_reserve(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __reserve_page__((void *)((uint64_t)(__address) + (_i * 4096)),
                     &reservedMem);
}

void *kernel_pmm_alloc() {
  for (; bitmapIndex < pageBitmap._Size * 8; bitmapIndex++) {
    if (kernel_bmp_get(&pageBitmap, bitmapIndex) == 0) {
      void *_page = (void *)(bitmapIndex * 4096);
      kernel_pmm_lock(_page, 1);
      return _page;
    }
  }

  return NULL; // Swap file not implemented yet
}

void kernel_pmm_initialize(struct limine_hhdm_response *__hhdm) {
  memoryMapResponse      = memoryMapRequest.response;
  uint64_t _highest_addr = 0;

  // Calculate memory map & bitmap size
  for (uint64_t _i = 0; _i < memoryMapResponse->entry_count; _i++) {
    struct limine_memmap_entry *_entry = memoryMapResponse->entries[_i];
    memSize += _entry->length;
    uint64_t _seg_top = _entry->base + _entry->length;

    kloginfo("PMM: Segment:\tBse=%u Length=%u Top=%u",
             _entry->base,
             _seg_top,
             _entry->length);

    if (_entry->type != LIMINE_MEMMAP_USABLE) {
      reservedMem += _entry->length;
      continue;
    }

    usablemem += _entry->length;
    if (_seg_top > _highest_addr)
      _highest_addr = _seg_top;
  }

  // Compute the actual size of the bitmap
  uint64_t _highest_pgindex = _highest_addr / 4096;
  uint64_t _bmp_sz          = _highest_pgindex / 8 + 1;

  // Find a segment that fits the bitmap and allocate it there
  for (uint64_t _i = 0; _i < memoryMapResponse->entry_count; _i++) {
    struct limine_memmap_entry *_entry = memoryMapResponse->entries[_i];

    if (_entry->type == LIMINE_MEMMAP_USABLE && _entry->length >= _bmp_sz) {
      kloginfo(
          "PMM: Bitmap:\tIndex=%u Base=%u Size=%u", _i, _entry->base, _bmp_sz);
      pageBitmap._Buffer = (uint8_t *)(_entry->base + __hhdm->offset);
      pageBitmap._Size   = _bmp_sz;

      // Reserve the entire memory space
      kmemset((void *)pageBitmap._Buffer, _bmp_sz, 0xff);

      // NOTE: This is just a small optiimzation
      // Shifting the base and length of the entry allows
      // us to skip reserving the bitmap.
      // Thus, when freeing this netry, the bitmap will still
      // be reserved
      _entry->base += _bmp_sz;
      _entry->length -= _bmp_sz;

      klogok("PMM: Memory reserved and bitmap allocated");

      // If a good-enough segment ahs been found
      // there's no need to continue
      break;
    }

    // Unreserve free memory
    for (uint64_t _i = 0; _i < memoryMapResponse->entry_count; _i++) {
      struct limine_memmap_entry *_entry = memoryMapResponse->entries[_i];

      if (_entry->type == LIMINE_MEMMAP_USABLE)
        kernel_pmm_unreserve((void *)_entry->base, _entry->length / 4096);
    }

    klogok("PMM: Done");
  }
}
