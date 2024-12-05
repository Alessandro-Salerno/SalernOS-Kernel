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
#include <arch/mmu.h>
#include <kernel/com/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/platform/info.h>
#include <kernel/platform/mmu.h>
#include <lib/mem.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>

#include "arch/cpu.h"

#define ADDRMASK (uint64_t)0x7ffffffffffff000
#define PTMASK   (uint64_t)0b111111111000000000000
#define PDMASK   (uint64_t)0b111111111000000000000000000000
#define PDPTMASK (uint64_t)0b111111111000000000000000000000000000000
#define PML4MASK (uint64_t)0b111111111000000000000000000000000000000000000000

#define DEPTH_PDPT 1
#define DEPTH_PD   2
#define DEPTH_PT   3

extern uint8_t _TEXT_START[];
extern uint8_t _TEXT_END[];
extern uint8_t _RODATA_START[];
extern uint8_t _RODATA_END[];
extern uint8_t _DATA_START[];
extern uint8_t _DATA_END[];
extern uint8_t _USER_TEXT_START[];
extern uint8_t _USER_TEXT_END[];
extern uint8_t _USER_DATA_START[];
extern uint8_t _USER_DATA_END[];

static arch_mmu_pagetable_t *RootTable = NULL;

static inline uint64_t *next(uint64_t entry) {
  if (0 == entry) {
    return NULL;
  }

  return (uint64_t *)ARCH_PHYS_TO_HHDM(entry & ADDRMASK);
}

static bool
add_page(arch_mmu_pagetable_t *top, void *vaddr, uint64_t entry, int depth) {
  uint64_t *pml4       = (uint64_t *)ARCH_PHYS_TO_HHDM(top);
  uintptr_t addr       = (uintptr_t)vaddr;
  uintptr_t ptoffset   = (addr & PTMASK) >> 12;
  uintptr_t pdoffset   = (addr & PDMASK) >> 21;
  uintptr_t pdptoffset = (addr & PDPTMASK) >> 30;
  uintptr_t pml4offset = (addr & PML4MASK) >> 39;

  if (DEPTH_PDPT == depth) {
    pml4[pml4offset] = entry;
    return true;
  }

  uint64_t *pdpt = next(pml4[pml4offset]);
  if (NULL == pdpt) {
    pdpt = com_mm_pmm_alloc();
    if (NULL == pdpt) {
      return false;
    }
    pml4[pml4offset] = (uint64_t)pdpt | ARCH_MMU_FLAGS_READ |
                       ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_USER;
    pdpt = (uint64_t *)ARCH_PHYS_TO_HHDM(pdpt);
    kmemset(pdpt, ARCH_PAGE_SIZE, 0);
  }

  if (DEPTH_PD == depth) {
    pdpt[pdptoffset] = entry;
    return true;
  }

  uint64_t *pd = next(pdpt[pdptoffset]);
  if (NULL == pd) {
    pd = com_mm_pmm_alloc();
    if (NULL == pd) {
      return false;
    }
    pdpt[pdptoffset] = (uint64_t)pd | ARCH_MMU_FLAGS_READ |
                       ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_USER;
    pd = (uint64_t *)ARCH_PHYS_TO_HHDM(pd);
    kmemset(pd, ARCH_PAGE_SIZE, 0);
  }

  if (DEPTH_PT == depth) {
    pd[pdoffset] = entry;
    return true;
  }

  uint64_t *pt = next(pd[pdoffset]);
  if (NULL == pt) {
    pt = com_mm_pmm_alloc();
    if (NULL == pt) {
      return false;
    }
    pd[pdoffset] = (uint64_t)pt | ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                   ARCH_MMU_FLAGS_USER;
    pt = (uint64_t *)ARCH_PHYS_TO_HHDM(pt);
    kmemset(pt, ARCH_PAGE_SIZE, 0);
  }

  pt[ptoffset] = entry;
  return true;
}

arch_mmu_pagetable_t *arch_mmu_new_table(void) {
  arch_mmu_pagetable_t *table = com_mm_pmm_alloc();

  if (NULL == table) {
    return NULL;
  }

  kmemcpy((void *)ARCH_PHYS_TO_HHDM(table), RootTable, ARCH_PAGE_SIZE);
  return table;
}

void arch_mmu_destroy_table(arch_mmu_pagetable_t *pt) {
  // TODO: implement this
}

arch_mmu_pagetable_t arch_mmu_duplicate_table(arch_mmu_pagetable_t *pt) {
  // TODO: implement this
}

bool arch_mmu_map(arch_mmu_pagetable_t *pt,
                  void                 *virt,
                  void                 *phys,
                  arch_mmu_flags_t      flags) {
  return add_page(pt, virt, ((uint64_t)phys & ADDRMASK) | flags, 0);
}

void arch_mmu_switch(arch_mmu_pagetable_t *pt) {
  DEBUG("switching to page table at address %x", pt);
  asm volatile("mov %%rax, %%cr3" : : "a"(pt));
}

bool arch_mmu_ispresent(arch_mmu_pagetable_t *pt, void *virt) {
  // TODO: implement this
}

bool arch_mmu_iswritable(arch_mmu_pagetable_t *pt, void *virt) {
  // TODO: implement this
}

bool arch_mmu_isdirty(arch_mmu_pagetable_t *pt, void *virt) {
  // TODO: implement this
}

// TODO: remove userspace mappings
void arch_mmu_init(void) {
  // Allocate the kernel (root) page table
  LOG("initializing mmu");
  RootTable = com_mm_pmm_alloc();
  ASSERT(NULL != RootTable);
  RootTable = (arch_mmu_pagetable_t *)ARCH_PHYS_TO_HHDM(RootTable);

  // Map the higher half into the new page table
  DEBUG("mapping higher half to kernel page table");
  for (uintmax_t i = 256; i < 512; i++) {
    uint64_t *entry = com_mm_pmm_alloc();
    ASSERT(NULL != entry);
    kmemset((void *)ARCH_PHYS_TO_HHDM(entry), ARCH_PAGE_SIZE, 0);
    RootTable[i] = (uint64_t)entry | ARCH_MMU_FLAGS_WRITE |
                   ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_USER;
  }

  // Mapp all the necessary memmap entries
  arch_memmap_t *memmap = arch_info_get_memmap();
  for (uintmax_t i = 0; i < memmap->entry_count; i++) {
    arch_memmap_entry_t *entry = memmap->entries[i];

    if (ARCH_MEMMAP_IS_MAPPABLE(entry)) {
      DEBUG("mapping page table entry range %x -> %x",
            entry->base,
            entry->base + entry->length);

      for (uintmax_t i = 0; i < entry->length; i += ARCH_PAGE_SIZE) {
        uint64_t pt_entry = ((entry->base + i) & ADDRMASK) |
                            ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                            ARCH_MMU_FLAGS_NOEXEC;
        ASSERT(add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                        (uint64_t *)ARCH_PHYS_TO_HHDM(entry->base + i),
                        pt_entry,
                        0));
      }
    }
  }

  // Map the kernel itself (using image map)
  DEBUG("mapping kernel image to page table");
  arch_kaddr_t *kaddr    = arch_info_get_kaddr();
  uint64_t      vp_delta = kaddr->virtual_base - kaddr->physical_base;
  DEBUG("kernel virt base: %x, kernel phys base: %x, delta: %x",
        kaddr->virtual_base,
        kaddr->physical_base,
        vp_delta);

  DEBUG("mapping kernel text section (virtual: %x -> %x)",
        _TEXT_START,
        _TEXT_END);
  for (uintptr_t i = (uintptr_t)_TEXT_START; i < (uintptr_t)_TEXT_END;
       i += ARCH_PAGE_SIZE) {
    ASSERT(add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                    (uint64_t *)i,
                    ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ,
                    0));
  }

  DEBUG("mapping kernel rodata section (virtual: %x -> %x)",
        _RODATA_START,
        _RODATA_END);
  for (uintptr_t i = (uintptr_t)_RODATA_START; i < (uintptr_t)_RODATA_END;
       i += ARCH_PAGE_SIZE) {
    ASSERT(add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                    (uint64_t *)i,
                    ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ |
                        ARCH_MMU_FLAGS_NOEXEC,
                    0));
  }

  DEBUG("mapping kernel data/bss/limine_requests sections (virtual: %x -> %x)",
        _DATA_START,
        _DATA_END);
  for (uintptr_t i = (uintptr_t)_DATA_START; i < (uintptr_t)_DATA_END;
       i += ARCH_PAGE_SIZE) {
    ASSERT(add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                    (uint64_t *)i,
                    ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ |
                        ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC,
                    0));
  }

  DEBUG("mapping user text section (virtual: %x -> %x)",
        _USER_TEXT_START,
        _USER_TEXT_END);
  for (uintptr_t i = (uintptr_t)_USER_TEXT_START; i < (uintptr_t)_USER_TEXT_END;
       i += ARCH_PAGE_SIZE) {
    ASSERT(add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                    (uint64_t *)i,
                    ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ |
                        ARCH_MMU_FLAGS_USER,
                    0));
  }

  DEBUG("mapping user data section (virtual: %x -> %x)",
        _USER_DATA_START,
        _USER_DATA_END);
  for (uintptr_t i = (uintptr_t)_USER_DATA_START; i < (uintptr_t)_USER_DATA_END;
       i += ARCH_PAGE_SIZE) {
    ASSERT(add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                    (uint64_t *)i,
                    ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ |
                        ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC |
                        ARCH_MMU_FLAGS_USER,
                    0));
  }

  arch_mmu_switch((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable));
  hdr_arch_cpu_get()->root_page_table = (void *)ARCH_HHDM_TO_PHYS(RootTable);
}
