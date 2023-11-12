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
#include <kmem.h>
#include <kstdio.h>
#include <limine.h>

#include "kernelpanic.h"
#include "liminebind.h"
#include "mm/bmp.h"
#include "mm/pmm.h"
#include "sched/lock.h"

static volatile struct limine_memmap_response *memoryMapResponse;

static bmp_t    pageBitmap;
static uint64_t bitmapIndex;

static uint64_t memSize;
static uint64_t usablemem;
static uint64_t freeMem;
static uint64_t reservedMem;
static uint64_t usedMem;

static lock_t pmmLock = SCHED_LOCK_NEW();

static void __reserve_page__(void *__address, uint64_t *__rsvmemcount) {
  uint64_t _idx = (uint64_t)(__address) / 4096;

  SOFTASSERT(!(bmp_get(&pageBitmap, _idx)), RETVOID)

  bmp_set(&pageBitmap, _idx, 1);
  freeMem -= 4096;
  *__rsvmemcount += 4096;
}

static void __unreserve_page__(void *__address, uint64_t *__rsvmemcount) {
  uint64_t _idx = (uint64_t)(__address) / 4096;

  SOFTASSERT(bmp_get(&pageBitmap, _idx), RETVOID)

  bmp_set(&pageBitmap, _idx, 0);
  freeMem += 4096;
  *__rsvmemcount -= 4096;

  bitmapIndex = (bitmapIndex > _idx) ? _idx : bitmapIndex;
}

void mm_pmm_free(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __unreserve_page__((void *)((uint64_t)(__address) + (_i * 4096)), &usedMem);
}

void mm_pmm_lock(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __reserve_page__((void *)((uint64_t)(__address) + (_i * 4096)), &usedMem);
}

void mm_pmm_unreserve(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __unreserve_page__((void *)((uint64_t)(__address) + (_i * 4096)),
                       &reservedMem);
}

void mm_pmm_reserve(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __reserve_page__((void *)((uint64_t)(__address) + (_i * 4096)),
                     &reservedMem);
}

void *mm_pmm_alloc() {
  sched_lock_acquire(&pmmLock);
  void *_ret = NULL;

  for (; bitmapIndex < pageBitmap._Size * 8; bitmapIndex++) {
    if (bmp_get(&pageBitmap, bitmapIndex) == 0) {
      _ret = (void *)(bitmapIndex * 4096);
      mm_pmm_lock(_ret, 1);
      break;
    }
  }

  sched_lock_release(&pmmLock);
  return _ret;
}

void mm_pmm_initialize() {
  memoryMapResponse      = liminebind_mmap_get();
  uint64_t _highest_addr = 0;

  // Calculate memory map & bitmap size
  for (uint64_t _i = 0; _i < memoryMapResponse->entry_count; _i++) {
    struct limine_memmap_entry *_entry = memoryMapResponse->entries[_i];
    memSize += _entry->length;
    uint64_t _seg_top = _entry->base + _entry->length;

    kloginfo("PMM: Segment:\tBse=%u Length=%u Top=%u",
             _entry->base,
             _entry->length,
             _seg_top);

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
      pageBitmap._Buffer = (uint8_t *)liminebind_transint(_entry->base);
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
        mm_pmm_unreserve((void *)_entry->base, _entry->length / 4096);
    }

    klogok("PMM: Done");
  }
}
