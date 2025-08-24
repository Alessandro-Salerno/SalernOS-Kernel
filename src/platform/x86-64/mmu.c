/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
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

#include <arch/cpu.h>
#include <arch/info.h>
#include <arch/mmu.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/platform/info.h>
#include <kernel/platform/mmu.h>
#include <lib/mem.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>

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

static uint64_t *get_page(arch_mmu_pagetable_t *top, void *vaddr) {
    uint64_t *pml4       = (uint64_t *)ARCH_PHYS_TO_HHDM(top);
    uintptr_t addr       = (uintptr_t)vaddr;
    uintptr_t ptoffset   = (addr & PTMASK) >> 12;
    uintptr_t pdoffset   = (addr & PDMASK) >> 21;
    uintptr_t pdptoffset = (addr & PDPTMASK) >> 30;
    uintptr_t pml4offset = (addr & PML4MASK) >> 39;

    uint64_t *pdpt = next(pml4[pml4offset]);
    if (NULL == pdpt) {
        return NULL;
    }

    uint64_t *pd = next(pdpt[pdptoffset]);
    if (NULL == pd) {
        return NULL;
    }

    uint64_t *pt = next(pd[pdoffset]);
    if (NULL == pt) {
        return NULL;
    }

    return pt + ptoffset;
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
        pd[pdoffset] = (uint64_t)pt | ARCH_MMU_FLAGS_READ |
                       ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_USER;
        pt = (uint64_t *)ARCH_PHYS_TO_HHDM(pt);
        kmemset(pt, ARCH_PAGE_SIZE, 0);
    }

    pt[ptoffset] = entry;
    return true;
}

// CREDIT: vloxei64/ke
static uint64_t duplicate_recursive(uint64_t entry, size_t level, size_t addr) {
    uint64_t *virt  = (uint64_t *)ARCH_PHYS_TO_HHDM(entry & ADDRMASK);
    uint64_t  new   = (uint64_t)com_mm_pmm_alloc();
    uint64_t *nvirt = (uint64_t *)ARCH_PHYS_TO_HHDM(new);

    if (level == 0) {
        kmemcpy(nvirt, virt, ARCH_PAGE_SIZE);
    } else {
        for (size_t i = 0; i < 512; i++) {
            if (ARCH_MMU_FLAGS_PRESENT & virt[i]) {
                nvirt[i] = duplicate_recursive(
                    virt[i], level - 1, addr | ((i << (12 + (level - 1) * 9))));
            } else {
                nvirt[i] = 0;
            }
        }
    }

    return new | (entry & ~ADDRMASK);
}

void destroy_recursive(uint64_t entry, size_t level) {
    uint64_t *directory = (void *)ARCH_PHYS_TO_HHDM(entry & ADDRMASK);
    void     *phys      = (void *)(entry & ADDRMASK);

    if (0 != level) {
        for (size_t i = 0; i < 512; i++) {
            if (ARCH_MMU_FLAGS_PRESENT & directory[i]) {
                destroy_recursive(directory[i], level - 1);
            }
        }
    }

    com_mm_pmm_free(phys);
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
    arch_mmu_pagetable_t *pt_virt = (void *)ARCH_PHYS_TO_HHDM(pt);

    for (size_t i = 0; i < 256; i++) {
        if (ARCH_MMU_FLAGS_PRESENT & pt_virt[i]) {
            destroy_recursive(pt_virt[i], 3);
        }
    }

    com_mm_pmm_free(pt);
}

arch_mmu_pagetable_t *arch_mmu_duplicate_table(arch_mmu_pagetable_t *pt) {
    arch_mmu_pagetable_t *new_pt = arch_mmu_new_table();

    if (NULL == new_pt) {
        return NULL;
    }

    arch_mmu_pagetable_t *new_virt =
        (arch_mmu_pagetable_t *)ARCH_PHYS_TO_HHDM(new_pt);
    arch_mmu_pagetable_t *pt_virt =
        (arch_mmu_pagetable_t *)ARCH_PHYS_TO_HHDM(pt);

    for (size_t i = 0; i < 256; i++) {
        if (ARCH_MMU_FLAGS_PRESENT & pt_virt[i]) {
            new_virt[i] = duplicate_recursive(pt_virt[i], 3, 0);
        }
    }

    return new_pt;
}

bool arch_mmu_map(arch_mmu_pagetable_t *pt,
                  void                 *virt,
                  void                 *phys,
                  arch_mmu_flags_t      flags) {
    return add_page(pt, virt, ((uint64_t)phys & ADDRMASK) | flags, 0);
}

void arch_mmu_switch(arch_mmu_pagetable_t *pt) {
    // KDEBUG("switching to page table at address %p", pt);
    asm volatile("mov %%rax, %%cr3" : : "a"(pt));
}

void arch_mmu_switch_default(void) {
    arch_mmu_switch((void *)ARCH_HHDM_TO_PHYS(RootTable));
}

// TODO: implement this
void *arch_mmu_get_physical(arch_mmu_pagetable_t *pagetable, void *virt_addr) {
    uint64_t *entry = get_page(pagetable, virt_addr);
    if (NULL == entry) {
        return NULL;
    }
    return (void *)((*entry & ADDRMASK) +
                    ((uintptr_t)virt_addr & (ARCH_PAGE_SIZE - 1)));
}

void arch_mmu_init(void) {
    // Allocate the kernel (root) page table
    KLOG("initializing mmu");
    RootTable = com_mm_pmm_alloc();
    KASSERT(NULL != RootTable);
    RootTable = (arch_mmu_pagetable_t *)ARCH_PHYS_TO_HHDM(RootTable);
    kmemset(RootTable, ARCH_PAGE_SIZE, 0);

    // Map the higher half into the new page table
    KDEBUG("mapping higher half to kernel page table");
    for (uintmax_t i = 256; i < 512; i++) {
        uint64_t *entry = com_mm_pmm_alloc();
        KASSERT(NULL != entry);
        kmemset((void *)ARCH_PHYS_TO_HHDM(entry), ARCH_PAGE_SIZE, 0);
        RootTable[i] = (uint64_t)entry | ARCH_MMU_FLAGS_WRITE |
                       ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_USER;
    }

    // Mapp all the necessary memmap entries
    arch_memmap_t *memmap = arch_info_get_memmap();
    for (uintmax_t i = 0; i < memmap->entry_count; i++) {
        arch_memmap_entry_t *entry = memmap->entries[i];

        if (ARCH_MEMMAP_IS_MAPPABLE(entry)) {
            KDEBUG("mapping page table entry range %p -> %p",
                   entry->base,
                   entry->base + entry->length);

            for (uintmax_t i = 0; i < entry->length; i += ARCH_PAGE_SIZE) {
                uint64_t pt_entry = ((entry->base + i) & ADDRMASK) |
                                    ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                                    ARCH_MMU_FLAGS_NOEXEC;
                KASSERT(add_page(
                    (arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                    (uint64_t *)ARCH_PHYS_TO_HHDM(entry->base + i),
                    pt_entry,
                    0));
            }
        }
    }

    // Map the kernel itself (using image map)
    KDEBUG("mapping kernel image to page table");
    arch_kaddr_t *kaddr    = arch_info_get_kaddr();
    uint64_t      vp_delta = kaddr->virtual_base - kaddr->physical_base;
    KDEBUG("kernel virt base: %p, kernel phys base: %p, delta: %p",
           kaddr->virtual_base,
           kaddr->physical_base,
           vp_delta);

    KDEBUG("mapping kernel text section (virtual: %p -> %p)",
           _TEXT_START,
           _TEXT_END);
    for (uintptr_t i = (uintptr_t)_TEXT_START; i < (uintptr_t)_TEXT_END;
         i += ARCH_PAGE_SIZE) {
        KASSERT(add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                         (uint64_t *)i,
                         ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ,
                         0));
    }

    KDEBUG("mapping kernel rodata section (virtual: %p -> %p)",
           _RODATA_START,
           _RODATA_END);
    for (uintptr_t i = (uintptr_t)_RODATA_START; i < (uintptr_t)_RODATA_END;
         i += ARCH_PAGE_SIZE) {
        KASSERT(add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                         (uint64_t *)i,
                         ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ |
                             ARCH_MMU_FLAGS_NOEXEC,
                         0));
    }

    KDEBUG(
        "mapping kernel data/bss/limine_requests sections (virtual: %p -> %p)",
        _DATA_START,
        _DATA_END);
    for (uintptr_t i = (uintptr_t)_DATA_START; i < (uintptr_t)_DATA_END;
         i += ARCH_PAGE_SIZE) {
        KASSERT(add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                         (uint64_t *)i,
                         ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ |
                             ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC,
                         0));
    }

    KDEBUG("mapping user text section (virtual: %p -> %p)",
           _USER_TEXT_START,
           _USER_TEXT_END);
    for (uintptr_t i = (uintptr_t)_USER_TEXT_START;
         i < (uintptr_t)_USER_TEXT_END;
         i += ARCH_PAGE_SIZE) {
        KASSERT(add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                         (uint64_t *)i,
                         ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ |
                             ARCH_MMU_FLAGS_USER,
                         0));
    }

    KDEBUG("mapping user data section (virtual: %p -> %p)",
           _USER_DATA_START,
           _USER_DATA_END);
    for (uintptr_t i = (uintptr_t)_USER_DATA_START;
         i < (uintptr_t)_USER_DATA_END;
         i += ARCH_PAGE_SIZE) {
        KASSERT(add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                         (uint64_t *)i,
                         ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ |
                             ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC |
                             ARCH_MMU_FLAGS_USER,
                         0));
    }

    arch_mmu_switch((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable));
    ARCH_CPU_GET()->root_page_table = (void *)ARCH_HHDM_TO_PHYS(RootTable);
}
