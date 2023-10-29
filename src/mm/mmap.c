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

#include "mm/mmap.h"

#include "kernelpanic.h"

#define MMAP_DINIT \
  "EFI Memory Map Fault:\nKernel tried to reinitialize Memory Map."
#define MMAP_NINIT                                                   \
  "EFI Memory Map Fault:\nKernel tried to fetch information before " \
  "initializing."

static meminfo_t mInfo;

static void    *mMap;
static uint64_t mMapSize;
static uint64_t mMapEntries;
static uint64_t mMapDescSize;

static uint64_t mSize;
static uint64_t mUsableSize;

static memseg_t mLargestSegment;

static bool mMapInitialized;

void kernel_mmap_initialize(meminfo_t __meminfo) {
  kernel_panic_assert(!mMapInitialized, MMAP_DINIT);

  mInfo        = __meminfo;
  mMap         = __meminfo._MemoryMap;
  mMapSize     = __meminfo._MemoryMapSize;
  mMapDescSize = __meminfo._DescriptorSize;

  mMapEntries = mMapSize / mMapDescSize;

  for (uint64_t _idx = 0; _idx < mMapEntries; _idx++) {
    efimemdesc_t *_entry  = kernel_mmap_entry_get(_idx);
    uint64_t      _seg_sz = _entry->_Pages * 4096;
    mSize += _seg_sz;
    mUsableSize += (_entry->_Type == USABLE_MEM_TYPE) * _seg_sz;

    if (_entry->_Type == 7 && _seg_sz > mLargestSegment._Size) {
      mLargestSegment =
          (memseg_t){._Segment = _entry->_PhysicalAddress, ._Size = _seg_sz};
    }
  }

  mMapInitialized = TRUE;
}

void kernel_mmap_info_get(uint64_t  *__memsz,
                          uint64_t  *__usablemem,
                          memseg_t  *__lseg,
                          meminfo_t *__meminfo) {
  kernel_panic_assert(mMapInitialized, MMAP_NINIT);

  ARGRET(__memsz, mSize);
  ARGRET(__usablemem, mUsableSize);
  ARGRET(__lseg, mLargestSegment);
  ARGRET(__meminfo, mInfo);
}

efimemdesc_t *kernel_mmap_entry_get(uint64_t __idx) {
  kernel_panic_assert(mMap != NULL, MMAP_NINIT);
  return (efimemdesc_t *)((uint64_t)(mMap) + (__idx * mMapDescSize));
}
