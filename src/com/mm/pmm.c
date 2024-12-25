/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#include <arch/info.h>
#include <kernel/com/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/panic.h>
#include <kernel/com/spinlock.h>
#include <kernel/platform/info.h>
#include <lib/mem.h>
#include <lib/printf.h>
#include <stdint.h>

typedef struct Bitmap {
  size_t    size;
  uint8_t  *buffer;
  uintmax_t index;
} bmp_t;

static bmp_t PageBitmap;

static uintmax_t MemSize;
static uintmax_t UsableMem;
static uintmax_t FreeMem;
static uintmax_t ReservedMem;
static uintmax_t UsedMem;

// TODO: turn this into a mutex
static com_spinlock_t Lock = COM_SPINLOCK_NEW();

static bool bmp_get(bmp_t *bmp, uintmax_t idx) {
  uintmax_t byte_idx    = idx / 8;
  uint8_t   bit_idx     = idx % 8;
  uint8_t   bit_indexer = 0b10000000 >> bit_idx;

  return (bmp->buffer[byte_idx] & bit_indexer) > 0;
}

static void bmp_set(bmp_t *bmp, uintmax_t idx, bool val) {
  uintmax_t byte_idx    = idx / 8;
  uint8_t   bit_idx     = idx % 8;
  uint8_t   bit_indexer = 0b10000000 >> bit_idx;

  bmp->buffer[byte_idx] &= ~bit_indexer;
  bmp->buffer[byte_idx] |= bit_indexer * val;
}

static void reserve_page(void *address, uint64_t *rsvmemcount) {
  uintmax_t idx = (uintmax_t)address / ARCH_PAGE_SIZE;

  ASSERT(!bmp_get(&PageBitmap, idx));

  bmp_set(&PageBitmap, idx, 1);
  FreeMem -= ARCH_PAGE_SIZE;
  *rsvmemcount += ARCH_PAGE_SIZE;
}

static void unreserve_page(void *address, uint64_t *rsvmemcount) {
  uintmax_t idx = (uintmax_t)address / ARCH_PAGE_SIZE;

  ASSERT(bmp_get(&PageBitmap, idx));

  bmp_set(&PageBitmap, idx, 0);
  FreeMem += ARCH_PAGE_SIZE;
  *rsvmemcount -= ARCH_PAGE_SIZE;

  if (PageBitmap.index > idx) {
    PageBitmap.index = idx;
  }
}

static void alloc_pages(void *address, uint64_t pagecount) {
  for (uintmax_t i = 0; i < pagecount; i++) {
    reserve_page((void *)((uintptr_t)address + (i * ARCH_PAGE_SIZE)), &UsedMem);
  }
}

void unreserve_pages(void *address, uint64_t pagecount) {
  for (uintmax_t i = 0; i < pagecount; i++) {
    unreserve_page((void *)((uintptr_t)address + (i * ARCH_PAGE_SIZE)),
                   &ReservedMem);
  }
}

void *com_mm_pmm_alloc() {
  hdr_com_spinlock_acquire(&Lock);
  void *ret = NULL;

  for (; PageBitmap.index < PageBitmap.size * 8; PageBitmap.index++) {
    if (0 == bmp_get(&PageBitmap, PageBitmap.index)) {
      ret = (void *)(PageBitmap.index * ARCH_PAGE_SIZE);
      alloc_pages(ret, 1);
      break;
    }
  }

  hdr_com_spinlock_release(&Lock);
  return ret;
}

void com_mm_pmm_free(void *page) {
  hdr_com_spinlock_acquire(&Lock);
  unreserve_page(page, &UsedMem);
  hdr_com_spinlock_release(&Lock);
}

void com_mm_pmm_get_info(uintmax_t *used_mem,
                         uintmax_t *free_mem,
                         uintmax_t *reserved_mem,
                         uintmax_t *sys_mem,
                         uintmax_t *mem_size) {
  if (NULL != used_mem) {
    *used_mem = UsedMem;
  }

  if (NULL != free_mem) {
    *free_mem = FreeMem;
  }

  if (NULL != reserved_mem) {
    *reserved_mem = ReservedMem;
  }

  if (NULL != sys_mem) {
    *sys_mem = UsableMem;
  }

  if (NULL != mem_size) {
    *mem_size = MemSize;
  }
}

void com_mm_pmm_init() {
  arch_memmap_t *memmap       = arch_info_get_memmap();
  uintptr_t      highest_addr = 0;

  // Calculate memory map & bitmap size
  for (uintmax_t i = 0; i < memmap->entry_count; i++) {
    arch_memmap_entry_t *entry = memmap->entries[i];
    MemSize += entry->length;
    uintptr_t seg_top = entry->base + entry->length;

    DEBUG("segment: base=%x top=%x usable=%u length=%u",
          entry->base,
          seg_top,
          ARCH_MEMMAP_IS_USABLE(entry),
          entry->length);

    if (!ARCH_MEMMAP_IS_USABLE(entry)) {
      ReservedMem += entry->length;
      continue;
    }

    UsableMem += entry->length;
    if (seg_top > highest_addr) {
      highest_addr = seg_top;
    }
  }

  DEBUG("searched all segments, found highest address at %x", highest_addr);

  // Compute the actual size of the bitmap
  uintptr_t highest_pgindex = highest_addr / ARCH_PAGE_SIZE;
  size_t    bmp_sz          = highest_pgindex / 8 + 1;

  DEBUG("memory is %u pages, bitmap needs %u bytes", highest_pgindex, bmp_sz);

  // Find a segment that fits the bitmap and allocate it there
  for (uintmax_t i = 0; i < memmap->entry_count; i++) {
    arch_memmap_entry_t *entry = memmap->entries[i];

    if (ARCH_MEMMAP_IS_USABLE(entry) && entry->length >= bmp_sz) {
      uintptr_t transbase = ARCH_PHYS_TO_HHDM(entry->base);
      DEBUG("bitmap: base=%x size=%u segment=%u", transbase, bmp_sz, i);
      PageBitmap.buffer = (uint8_t *)transbase;
      PageBitmap.size   = bmp_sz;

      // Reserve the entire memory space
      kmemset((void *)PageBitmap.buffer, bmp_sz, 0xff);

      // If a good-enough segment has been found
      // there's no need to continue
      break;
    }
  }

  LOG("freeing usable pages");

  // Unreserve free memory
  for (uintmax_t i = 0; i < memmap->entry_count; i++) {
    arch_memmap_entry_t *entry = memmap->entries[i];
    void                *base  = (void *)entry->base;
    uintmax_t            len   = entry->length;

    // Avoid unreserving the page bitmap
    // you don't want that
    if ((void *)ARCH_HHDM_TO_PHYS(PageBitmap.buffer) == base) {
      base += PageBitmap.size;
      len -= PageBitmap.size;
    }

    if (ARCH_MEMMAP_IS_USABLE(entry)) {
      unreserve_pages(base, len / ARCH_PAGE_SIZE);
    }
  }
}
