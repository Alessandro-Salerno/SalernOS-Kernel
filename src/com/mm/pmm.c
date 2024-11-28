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
#include <kernel/com/panic.h>
#include <kernel/com/spinlock.h>
#include <lib/mem.h>
#include <lib/printf.h>
#include <vendor/limine.h>

typedef struct Bitmap {
  size_t   size;
  uint8_t *buffer;
} bmp_t;

bool bmp_get(bmp_t *bmp, uint64_t idx) {
  uint64_t byte_idx    = idx / 8;
  uint8_t  bit_idx     = idx % 8;
  uint8_t  bit_indexer = 0b10000000 >> bit_idx;

  return (bmp->buffer[byte_idx] & bit_indexer) > 0;
}

void bmp_set(bmp_t *bmp, uint64_t idx, bool val) {
  uint64_t byte_idx    = idx / 8;
  uint8_t  bit_idx     = idx % 8;
  uint8_t  bit_indexer = 0b10000000 >> bit_idx;

  bmp->buffer[byte_idx] &= ~bit_indexer;
  bmp->buffer[byte_idx] |= bit_indexer * val;
}

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_memmap_request
    memoryMapRequest = {.id = LIMINE_MEMMAP_REQUEST, .revision = 0};

static volatile struct limine_hhdm_request HhdmRequest = {
    .id       = LIMINE_HHDM_REQUEST,
    .revision = 0};

static uint64_t liminebind_transint(uint64_t addr) {
  return addr + (uint64_t)HhdmRequest.response->offset;
}

static bmp_t    pageBitmap;
static uint64_t bitmapIndex;

static uint64_t MemSize;
static uint64_t UsableMem;
static uint64_t FreeMem;
static uint64_t ReservedMem;
static uint64_t UsedMem;

static spinlock_t pmmLock = SPINLOCK_NEW();

static void reserve_page(void *address, uint64_t *rsvmemcount) {
  uint64_t idx = (uint64_t)(address) / 4096;

  if (bmp_get(&pageBitmap, idx)) {
    return;
  }

  bmp_set(&pageBitmap, idx, 1);
  FreeMem -= 4096;
  *rsvmemcount += 4096;
}

static void unreserve_page(void *address, uint64_t *rsvmemcount) {
  uint64_t idx = (uint64_t)(address) / 4096;

  if (!bmp_get(&pageBitmap, idx)) {
    return;
  }

  bmp_set(&pageBitmap, idx, 0);
  FreeMem += 4096;
  *rsvmemcount -= 4096;

  bitmapIndex = (bitmapIndex > idx) ? idx : bitmapIndex;
}

static void reserve_pages(void *address, uint64_t pagecount) {
  for (uint64_t i = 0; i < pagecount; i++) {
    reserve_page((void *)((uint64_t)(address) + (i * 4096)), &UsedMem);
  }
}

void unreserve_pages(void *address, uint64_t pagecount) {
  for (uint64_t i = 0; i < pagecount; i++) {
    unreserve_page((void *)((uint64_t)(address) + (i * 4096)), &ReservedMem);
  }
}

void *com_mm_pmm_alloc() {
  hdr_com_spinlock_acquire(&pmmLock);
  void *ret = NULL;

  for (; bitmapIndex < pageBitmap.size * 8; bitmapIndex++) {
    if (bmp_get(&pageBitmap, bitmapIndex) == 0) {
      ret = (void *)(bitmapIndex * 4096);
      reserve_pages(ret, 1);
      break;
    }
  }

  hdr_com_spinlock_release(&pmmLock);
  return ret;
}

void com_mm_pmm_init() {
  struct limine_memmap_response *memmap       = memoryMapRequest.response;
  uint64_t                       highest_addr = 0;

  // Calculate memory map & bitmap size
  for (uint64_t i = 0; i < memmap->entry_count; i++) {
    struct limine_memmap_entry *entry = memmap->entries[i];
    MemSize += entry->length;
    uint64_t seg_top = entry->base + entry->length;

    DEBUG("segment: bse=%x length=%u top=%x usable=%u",
          entry->base,
          entry->length,
          seg_top,
          entry->type == LIMINE_MEMMAP_USABLE);

    if (entry->type == LIMINE_MEMMAP_USABLE) {
      UsableMem += entry->length;
      if (seg_top > highest_addr)
        highest_addr = seg_top;
      continue;
    }

    ReservedMem += entry->length;
  }

  DEBUG("searched all segments, found highest address at %x", highest_addr);

  // Compute the actual size of the bitmap
  uint64_t highest_pgindex = highest_addr / 4096;
  uint64_t bmp_sz          = highest_pgindex / 8 + 1;

  DEBUG("bitmap requirements: size=%u bytes", bmp_sz);

  // Find a segment that fits the bitmap and allocate it there
  for (uint64_t i = 0; i < memmap->entry_count; i++) {
    struct limine_memmap_entry *entry = memmap->entries[i];

    if (entry->type == LIMINE_MEMMAP_USABLE && entry->length >= bmp_sz) {
      uint64_t transbase = liminebind_transint(entry->base);
      DEBUG("bitmap: index=%x base=%x size=%x", i, transbase, bmp_sz);
      pageBitmap.buffer = (uint8_t *)transbase;
      pageBitmap.size   = bmp_sz;

      // Reserve the entire memory space
      kmemset((void *)pageBitmap.buffer, bmp_sz, 0xff);

      // NOTE: This is just a small optiimzation
      // Shifting the base and length of the entry allows
      // us to skip reserving the bitmap.
      // Thus, when freeing this netry, the bitmap will still
      // be reserved
      entry->base += bmp_sz;
      entry->length -= bmp_sz;

      LOG("memory reserved and bitmap allocated");

      // If a good-enough segment ahs been found
      // there's no need to continue
      break;
    }
  }

  // Unreserve free memory
  for (uint64_t i = 0; i < memmap->entry_count; i++) {
    struct limine_memmap_entry *entry = memmap->entries[i];

    if (entry->type == LIMINE_MEMMAP_USABLE) {
      unreserve_pages((void *)entry->base, entry->length / 4096);
    }
  }
}
