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

#include "mm/pgfalloc.h"
#include <kmem.h>

#include "kernelpanic.h"

#define PGFALLOC_FAULT                                             \
  "Page Frame Allocator Fault:\nAllocator Function called before " \
  "initialization."
#define PGFALLOC_DBMP \
  "Page Frame Allocator Fault:\nAllocator tried to reinitialize page bitmap."

static uint64_t mFreeSize, mReservedSize, mUsedSize;

static uint64_t pgBmpIndex;

static bmp_t    pgBitmap;
static memseg_t mSegment;

static uint8_t pgfInitialized;

static void __reserve_page__(void *__address) {
  kernel_panic_assert(pgfInitialized >= 2, PGFALLOC_FAULT);

  uint64_t _idx = (uint64_t)(__address) / 4096;

  SOFTASSERT(!(kernel_bmp_get(&pgBitmap, _idx)), RETVOID)

  kernel_bmp_set(&pgBitmap, _idx, 1);
  mFreeSize -= 4096;
  mReservedSize += 4096;
}

static void __unreserve_page__(void *__address) {
  kernel_panic_assert(pgfInitialized >= 2, PGFALLOC_FAULT);

  uint64_t _idx = (uint64_t)(__address) / 4096;

  SOFTASSERT(kernel_bmp_get(&pgBitmap, _idx), RETVOID)

  kernel_bmp_set(&pgBitmap, _idx, 0);
  mFreeSize += 4096;
  mReservedSize -= 4096;

  pgBmpIndex = (pgBmpIndex > _idx) ? _idx : pgBmpIndex;
}

static void __init__bitmap__() {
  kernel_panic_assert(pgfInitialized == 1, PGFALLOC_DBMP);

  kmemset(mSegment._Segment, mSegment._Size, 0);

  pgBitmap = (bmp_t){._Size   = mFreeSize / 4096 / 8 + 1,
                     ._Buffer = (uint8_t *)(mSegment._Segment)};

  pgfInitialized = 2;
}

void __free_page__(void *__address) {
  kernel_panic_assert(pgfInitialized >= 2, PGFALLOC_FAULT);

  uint64_t _idx = (uint64_t)(__address) / 4096;

  SOFTASSERT(kernel_bmp_get(&pgBitmap, _idx), RETVOID)

  kernel_bmp_set(&pgBitmap, _idx, 0);
  mFreeSize += 4096;
  mUsedSize -= 4096;

  pgBmpIndex = (pgBmpIndex > _idx) ? _idx : pgBmpIndex;
}

void __lock__page__(void *__address) {
  kernel_panic_assert(pgfInitialized >= 2, PGFALLOC_FAULT);

  uint64_t _idx = (uint64_t)(__address) / 4096;

  SOFTASSERT(!(kernel_bmp_get(&pgBitmap, _idx)), RETVOID)

  kernel_bmp_set(&pgBitmap, _idx, 1);
  mFreeSize -= 4096;
  mUsedSize += 4096;
}

void kernel_pgfa_initialize() {
  SOFTASSERT(pgfInitialized == 0, RETVOID);

  // Temporary code
  memseg_t  _bmp_seg;
  meminfo_t _meminfo;
  kernel_mmap_info_get(&mFreeSize, NULL, &_bmp_seg, &_meminfo);
  mSegment = _bmp_seg;

  pgfInitialized = TRUE;
  __init__bitmap__();

  kernel_pgfa_reserve(0, mFreeSize / 4096 + 1);

  for (uint64_t _i = 0; _i < _meminfo._MemoryMapSize / _meminfo._DescriptorSize;
       _i++) {
    efimemdesc_t *_mem_desc = kernel_mmap_entry_get(_i);

    if (_mem_desc->_Type == USABLE_MEM_TYPE)
      kernel_pgfa_unreserve(_mem_desc->_PhysicalAddress, _mem_desc->_Pages);
  }

  kernel_pgfa_reserve(0, 0x100);
  kernel_pgfa_lock(pgBitmap._Buffer, pgBitmap._Size / 4096 + 1);
  pgfInitialized = 3;
}

void kernel_pgfa_free(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __free_page__((void *)((uint64_t)(__address) + (_i * 4096)));
}

void kernel_pgfa_lock(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __lock__page__((void *)((uint64_t)(__address) + (_i * 4096)));
}

void kernel_pgfa_unreserve(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __unreserve_page__((void *)((uint64_t)(__address) + (_i * 4096)));
}

void kernel_pgfa_reserve(void *__address, uint64_t __pagecount) {
  for (uint64_t _i = 0; _i < __pagecount; _i++)
    __reserve_page__((void *)((uint64_t)(__address) + (_i * 4096)));
}

void kernel_pgfa_info_get(uint64_t *__freemem,
                          uint64_t *__usedmem,
                          uint64_t *__reservedmem) {
  ARGRET(__freemem, mFreeSize);
  ARGRET(__usedmem, mUsedSize);
  ARGRET(__reservedmem, mReservedSize);
}

void *kernel_pgfa_page_new() {
  kernel_panic_assert(pgfInitialized >= 2, PGFALLOC_FAULT);

  for (; pgBmpIndex < pgBitmap._Size * 8; pgBmpIndex++) {
    if (kernel_bmp_get(&pgBitmap, pgBmpIndex) == 0) {
      void *_page = (void *)(pgBmpIndex * 4096);
      __lock__page__(_page);
      return _page;
    }
  }

  return NULL; // Swap file not implemented yet
}
