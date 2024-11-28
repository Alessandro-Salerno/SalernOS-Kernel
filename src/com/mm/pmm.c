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

#include <kernel/com/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/panic.h>
#include <kernel/com/spinlock.h>
#include <lib/mem.h>
#include <lib/printf.h>
#include <stdint.h>
#include <vendor/limine.h>

typedef struct Bitmap {
  size_t    size;
  uint8_t  *buffer;
  uintmax_t index;
} bmp_t;

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_memmap_request
    MemoryMapRequest = {.id = LIMINE_MEMMAP_REQUEST, .revision = 0};

static volatile struct limine_hhdm_request HhdmRequest = {
    .id       = LIMINE_HHDM_REQUEST,
    .revision = 0};

static bmp_t PageBitmap;

static uintmax_t MemSize;
static uintmax_t UsableMem;
static uintmax_t FreeMem;
static uintmax_t ReservedMem;
static uintmax_t UsedMem;

// TODO: turn this into a mutex
static spinlock_t Lock = SPINLOCK_NEW();

// TODO: create some kind of global thing for this
static inline uintptr_t liminebind_transint(uintptr_t addr) {
  return addr + (uintptr_t)HhdmRequest.response->offset;
}

bool bmp_get(bmp_t *bmp, uintmax_t idx) {
  uintmax_t byte_idx    = idx / 8;
  uint8_t   bit_idx     = idx % 8;
  uint8_t   bit_indexer = 0b10000000 >> bit_idx;

  return (bmp->buffer[byte_idx] & bit_indexer) > 0;
}

void bmp_set(bmp_t *bmp, uintmax_t idx, bool val) {
  uintmax_t byte_idx    = idx / 8;
  uint8_t   bit_idx     = idx % 8;
  uint8_t   bit_indexer = 0b10000000 >> bit_idx;

  bmp->buffer[byte_idx] &= ~bit_indexer;
  bmp->buffer[byte_idx] |= bit_indexer * val;
}

static void reserve_page(void *address, uint64_t *rsvmemcount) {
  uintmax_t idx = (uintmax_t)address / 4096;

  ASSERT(!bmp_get(&PageBitmap, idx));

  bmp_set(&PageBitmap, idx, 1);
  FreeMem -= 4096;
  *rsvmemcount += 4096;
}

static void unreserve_page(void *address, uint64_t *rsvmemcount) {
  uintmax_t idx = (uintmax_t)address / 4096;

  ASSERT(bmp_get(&PageBitmap, idx));

  bmp_set(&PageBitmap, idx, 0);
  FreeMem += 4096;
  *rsvmemcount -= 4096;

  if (PageBitmap.index > idx) {
    PageBitmap.index = idx;
  }
}

static void alloc_pages(void *address, uint64_t pagecount) {
  for (uintmax_t i = 0; i < pagecount; i++) {
    reserve_page((void *)((uintptr_t)address + (i * 4096)), &UsedMem);
  }
}

void unreserve_pages(void *address, uint64_t pagecount) {
  for (uintmax_t i = 0; i < pagecount; i++) {
    unreserve_page((void *)((uintptr_t)address + (i * 4096)), &ReservedMem);
  }
}

void *com_mm_pmm_alloc() {
  hdr_com_spinlock_acquire(&Lock);
  void *ret = NULL;

  for (; PageBitmap.index < PageBitmap.size * 8; PageBitmap.index++) {
    if (0 == bmp_get(&PageBitmap, PageBitmap.index)) {
      ret = (void *)(PageBitmap.index * 4096);
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

// TODO: make this arch-agnostic and limine-agnostic
//        the solution would proably be to take all this info
//        as parameters and let the implementation figure this out
void com_mm_pmm_init() {
  struct limine_memmap_response *memmap       = MemoryMapRequest.response;
  uintptr_t                      highest_addr = 0;

  ASSERT(NULL != memmap);

  // Calculate memory map & bitmap size
  for (uintmax_t i = 0; i < memmap->entry_count; i++) {
    struct limine_memmap_entry *entry = memmap->entries[i];
    MemSize += entry->length;
    uintptr_t seg_top = entry->base + entry->length;

    DEBUG("segment: base=%x top=%x usable=%u length=%u",
          entry->base,
          seg_top,
          LIMINE_MEMMAP_USABLE == entry->type,
          entry->length);

    if (LIMINE_MEMMAP_USABLE != entry->type) {
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
  uintptr_t highest_pgindex = highest_addr / 4096;
  size_t    bmp_sz          = highest_pgindex / 8 + 1;

  DEBUG("memory is %u pages, bitmap needs %u bytes", highest_pgindex, bmp_sz);

  // Find a segment that fits the bitmap and allocate it there
  for (uintmax_t i = 0; i < memmap->entry_count; i++) {
    struct limine_memmap_entry *entry = memmap->entries[i];

    if (LIMINE_MEMMAP_USABLE == entry->type && entry->length >= bmp_sz) {
      uintptr_t transbase = liminebind_transint(entry->base);
      DEBUG("bitmap: base=%x size=%u segment=%u", transbase, bmp_sz, i);
      PageBitmap.buffer = (uint8_t *)transbase;
      PageBitmap.size   = bmp_sz;

      // Reserve the entire memory space
      kmemset((void *)PageBitmap.buffer, bmp_sz, 0xff);

      // NOTE: This is just a small optiimzation
      // Shifting the base and length of the entry allows
      // us to skip reserving the bitmap.
      // Thus, when freeing this entry, the bitmap will still
      // be reserved
      entry->base += bmp_sz;
      entry->length -= bmp_sz;

      // If a good-enough segment has been found
      // there's no need to continue
      break;
    }
  }

  LOG("freeing usable pages");

  // Unreserve free memory
  for (uintmax_t i = 0; i < memmap->entry_count; i++) {
    struct limine_memmap_entry *entry = memmap->entries[i];

    if (LIMINE_MEMMAP_USABLE == entry->type) {
      unreserve_pages((void *)entry->base, entry->length / 4096);
    }
  }
}
