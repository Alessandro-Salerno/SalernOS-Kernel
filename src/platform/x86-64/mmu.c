/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
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
#include <kernel/com/mm/pmmcache.h>
#include <kernel/platform/info.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/mmu.h>
#include <kernel/platform/x86-64/smp.h>
#include <lib/mem.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>

#define PT_CACHE_INIT_SIZE     1024
#define PT_CACHE_POOL_SIZE     512
#define PT_CACHE_MAX_POOL_SIZE 4096

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

static inline void *internal_alloc_phys(void) {
    arch_cpu_t *curr_cpu = ARCH_CPU_GET();
    return com_mm_pmm_cache_alloc(&curr_cpu->mmu_cache, 1);
}

static inline void internal_free_phys(void *page) {
    arch_cpu_t *curr_cpu = ARCH_CPU_GET();
    com_mm_pmm_cache_free(&curr_cpu->mmu_cache, page);
}

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
        pdpt = internal_alloc_phys();
        if (NULL == pdpt) {
            return false;
        }
        pml4[pml4offset] = (uint64_t)pdpt | ARCH_MMU_FLAGS_READ |
                           ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_USER;
        pdpt = (uint64_t *)ARCH_PHYS_TO_HHDM(pdpt);
    }

    if (DEPTH_PD == depth) {
        pdpt[pdptoffset] = entry;
        return true;
    }

    uint64_t *pd = next(pdpt[pdptoffset]);
    if (NULL == pd) {
        pd = internal_alloc_phys();
        if (NULL == pd) {
            return false;
        }
        pdpt[pdptoffset] = (uint64_t)pd | ARCH_MMU_FLAGS_READ |
                           ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_USER;
        pd = (uint64_t *)ARCH_PHYS_TO_HHDM(pd);
    }

    if (DEPTH_PT == depth) {
        pd[pdoffset] = entry;
        return true;
    }

    uint64_t *pt = next(pd[pdoffset]);
    if (NULL == pt) {
        pt = internal_alloc_phys();
        if (NULL == pt) {
            return false;
        }
        pd[pdoffset] = (uint64_t)pt | ARCH_MMU_FLAGS_READ |
                       ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_USER;
        pt = (uint64_t *)ARCH_PHYS_TO_HHDM(pt);
    }

    pt[ptoffset] = entry;
    return true;
}

// This must be called with the guarantee that the target of the TLB shootdown
// is the current CPU. i.e., only if curr_pt == shootdown_pt
static inline void internal_invalidate(void *virt, size_t pages) {
    if (NULL == virt || pages > 32) {
        // this puts the current cr3 back into cr3 which causes a full TLB
        // shootdown
        asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3;"
                     :
                     :
                     : "rax", "memory");
        return;
    }

    // If we have a smaller invalidation, it is faster to perform invlpg
    for (size_t i = 0; i < pages; i++) {
        uintptr_t ptr = (uintptr_t)virt + i * ARCH_PAGE_SIZE;
        asm volatile("invlpg (%%rax)" : : "a"(ptr) : "memory");
    }
}

// CREDIT: vloxei64/ke
static uint64_t duplicate_recursive(uint64_t entry, size_t level, size_t addr) {
    uint64_t *virt           = (uint64_t *)ARCH_PHYS_TO_HHDM(entry & ADDRMASK);
    bool      entry_shared   = ARCH_MMU_EXTRA_FLAGS_SHARED & entry;
    bool      entry_writable = ARCH_MMU_FLAGS_WRITE & entry;

    if (!(ARCH_MMU_FLAGS_PRESENT & entry)) {
        return 0;
    }

    if (0 == level) {
        com_mm_pmm_hold((void *)(entry & ADDRMASK));
        return (entry_shared)
                   ? entry
                   : (entry & ~ARCH_MMU_FLAGS_WRITE) | (entry_writable << 9);
    }

    uint64_t new    = (uint64_t)internal_alloc_phys();
    uint64_t *nvirt = (uint64_t *)ARCH_PHYS_TO_HHDM(new);

    for (size_t i = 0; i < 512; i++) {
        if (ARCH_MMU_FLAGS_PRESENT & virt[i]) {
            nvirt[i] = duplicate_recursive(virt[i],
                                           level - 1,
                                           addr |
                                               ((i << (12 + (level - 1) * 9))));
            if (1 == level) {
                virt[i] = nvirt[i];
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

        internal_free_phys(phys);
    } else {
        // If we're at level 0, then this page is not part of the PT and needs
        // to be returned to pmm
        com_mm_pmm_free(phys);
    }
}

arch_mmu_pagetable_t *arch_mmu_new_table(void) {
    arch_mmu_pagetable_t *table = internal_alloc_phys();

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

    internal_free_phys(pt);
}

arch_mmu_pagetable_t *arch_mmu_duplicate_table(arch_mmu_pagetable_t *pt) {
    arch_mmu_pagetable_t *new_pt = arch_mmu_new_table();

    if (NULL == new_pt) {
        return NULL;
    }

    arch_mmu_pagetable_t *new_virt = (arch_mmu_pagetable_t *)ARCH_PHYS_TO_HHDM(
        new_pt);
    arch_mmu_pagetable_t *pt_virt = (arch_mmu_pagetable_t *)ARCH_PHYS_TO_HHDM(
        pt);

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

bool arch_mmu_chflags(arch_mmu_flags_t *pt,
                      void             *virt,
                      arch_mmu_flags_t  new_flags) {
    uint64_t *entry = get_page(pt, virt);
    if (NULL == entry) {
        return false;
    }
    *entry = (*entry & ADDRMASK) | new_flags;
    return true;
}

bool arch_mmu_unmap(void **out_old_phys, arch_mmu_pagetable_t *pt, void *virt) {
    uint64_t *entry = get_page(pt, virt);
    if (NULL == entry) {
        return false;
    }
    *out_old_phys = (void *)((*entry) & ADDRMASK);
    *entry        = 0;
    return true;
}

// CREDIT: Mathewnd/Astral
void arch_mmu_invalidate(arch_mmu_pagetable_t *pt, void *virt, size_t pages) {
    com_thread_t         *curr_thread = ARCH_CPU_GET_THREAD();
    arch_cpu_t           *curr_cpu    = ARCH_CPU_GET();
    arch_mmu_pagetable_t *curr_pt     = arch_mmu_get_table();

    // as of kernel 0.2.4, NULL is still almost always a legal userspace
    // addres since we don't have ELF slide working properly, so that check
    // is redundant, but we'll keep it just in case.
    bool is_user = virt > ARCH_MMU_USERSPACE_START &&
                   (virt + pages * ARCH_PAGE_SIZE) < ARCH_MMU_KERNELSPACE_START;
    bool do_shootdown = NULL != curr_thread &&
                        (virt >= ARCH_MMU_KERNELSPACE_START ||
                         ((NULL == virt || is_user) &&
                          NULL != curr_thread->proc &&
                          __atomic_load_n(
                              &curr_thread->proc->num_running_threads,
                              __ATOMIC_SEQ_CST) > 1));
    // numer of other CPUs that have completed the shootdown
    size_t shootdown_counter = 0;
    // expected number of CPUs to compelte the shootdown
    size_t num_other_cpus = 0;

    arch_cpu_t *next_cpu;
    for (size_t i = 0;
         do_shootdown && NULL != (next_cpu = x86_64_smp_get_cpu(i));
         i++) {
        // Skipp current CPU, we'll do that later
        if (next_cpu->id == curr_cpu->id) {
            continue;
        }
        // Skip CPUs that are not running any process or are running a different
        // process then the one we're targetting
        kspinlock_acquire(&next_cpu->runqueue_lock);
        com_thread_t *next_cpu_thread = next_cpu->thread;
        if (NULL == next_cpu_thread || NULL == next_cpu_thread->proc ||
            pt != next_cpu_thread->proc->vmm_context->pagetable) {
            kspinlock_release(&next_cpu->runqueue_lock);
            continue;
        }
        kspinlock_release(&next_cpu->runqueue_lock);

        // Now here we know we have ourselves a CPU we can target. At the very
        // worst it will context switch and receivea spurious IPI and we loose a
        // little bit of performance
        kspinlock_acquire(&next_cpu->mmu_shootdown_lock);
        next_cpu->mmu_shootdown_counter = &shootdown_counter;
        next_cpu->mmu_shootdown_virt    = virt;
        next_cpu->mmu_shootdown_pages   = pages;
        kspinlock_fake_release();
        // lock released by ISR
        num_other_cpus++;
        ARCH_CPU_SEND_IPI(next_cpu, X86_64_MMU_IPI_INVALIDATE);
    }

    // If we also have to perform a shootdown
    if (pt == curr_pt) {
        kspinlock_acquire(&curr_cpu->mmu_shootdown_lock);
        internal_invalidate(virt, pages);
        kspinlock_release(&curr_cpu->mmu_shootdown_lock);
    }

    // And finally, we wait for all others to complete
    while (do_shootdown &&
           __atomic_load_n(&shootdown_counter, __ATOMIC_RELAXED) !=
               num_other_cpus) {
        ARCH_CPU_PAUSE();
    }
}

void arch_mmu_switch(arch_mmu_pagetable_t *pt) {
    // KDEBUG("switching to page table at address %p", pt);
    asm volatile("mov %%rax, %%cr3" : : "a"(pt));
}

void arch_mmu_switch_default(void) {
    arch_mmu_switch((void *)ARCH_HHDM_TO_PHYS(RootTable));
}

void *arch_mmu_get_physical(arch_mmu_pagetable_t *pagetable, void *virt_addr) {
    uint64_t *entry = get_page(pagetable, virt_addr);
    if (NULL == entry) {
        return NULL;
    }
    if (0 == (*entry & ADDRMASK)) {
        return NULL;
    }
    return (void *)((*entry & ADDRMASK) +
                    ((uintptr_t)virt_addr & (ARCH_PAGE_SIZE - 1)));
}

bool arch_mmu_is_cow(arch_mmu_pagetable_t *pagetable, void *virt_addr) {
    uint64_t *entry = get_page(pagetable, virt_addr);
    if (NULL == entry) {
        return false;
    }
    return ARCH_MMU_EXTRA_FLAGS_PRIVATE & *entry;
}

bool arch_mmu_is_executable(arch_mmu_pagetable_t *pagetable, void *virt_addr) {
    uint64_t *entry = get_page(pagetable, virt_addr);
    if (NULL == entry) {
        return false;
    }
    return !(ARCH_MMU_FLAGS_NOEXEC & *entry);
}

arch_mmu_pagetable_t *arch_mmu_get_table(void) {
    void *pt;
    asm volatile("mov %%cr3, %0" : "=r"(pt));
    return pt;
}

void arch_mmu_init(void) {
    // Allocate the kernel (root) page table
    KLOG("initializing mmu");
    x86_64_mmu_init_cpu();
    RootTable = internal_alloc_phys();
    KASSERT(NULL != RootTable);
    RootTable = (arch_mmu_pagetable_t *)ARCH_PHYS_TO_HHDM(RootTable);

    // Map the higher half into the new page table
    KDEBUG("mapping higher half to kernel page table");
    for (uintmax_t i = 256; i < 512; i++) {
        uint64_t *entry = internal_alloc_phys();
        KASSERT(NULL != entry);
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

            arch_mmu_flags_t other_flags = 0;
            if (ARCH_MEMMAP_IS_FRAMEBUFFER(entry)) {
                other_flags |= ARCH_MMU_FLAGS_WC;
            }

            for (uintmax_t i = 0; i < entry->length; i += ARCH_PAGE_SIZE) {
                uint64_t pt_entry = ((entry->base + i) & ADDRMASK) |
                                    ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                                    ARCH_MMU_FLAGS_NOEXEC | other_flags;
                KASSERT_CALL_SUCCESSFUL(add_page(
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
        KASSERT_CALL_SUCCESSFUL(
            add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                     (uint64_t *)i,
                     ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ,
                     0));
    }

    KDEBUG("mapping kernel rodata section (virtual: %p -> %p)",
           _RODATA_START,
           _RODATA_END);
    for (uintptr_t i = (uintptr_t)_RODATA_START; i < (uintptr_t)_RODATA_END;
         i += ARCH_PAGE_SIZE) {
        KASSERT_CALL_SUCCESSFUL(
            add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
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
        KASSERT_CALL_SUCCESSFUL(
            add_page((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable),
                     (uint64_t *)i,
                     ((i - vp_delta) & ADDRMASK) | ARCH_MMU_FLAGS_READ |
                         ARCH_MMU_FLAGS_WRITE | ARCH_MMU_FLAGS_NOEXEC,
                     0));
    }

    arch_mmu_switch((arch_mmu_pagetable_t *)ARCH_HHDM_TO_PHYS(RootTable));
    ARCH_CPU_GET()->root_page_table = (void *)ARCH_HHDM_TO_PHYS(RootTable);
}

void x86_64_mmu_init_cpu(void) {
    arch_cpu_t *curr_cpu = ARCH_CPU_GET();
    if (NULL != curr_cpu->mmu_cache.private.pool) {
        return;
    }

    com_mm_pmm_cache_init(&curr_cpu->mmu_cache,
                          PT_CACHE_INIT_SIZE,
                          PT_CACHE_POOL_SIZE,
                          PT_CACHE_MAX_POOL_SIZE,
                          COM_MM_PMM_CACHE_FLAGS_AUTOALLOC |
                              COM_MM_PMM_CACHE_FLAGS_AUTOLOCK);
}

void x86_64_mmu_fault_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;

    arch_mmu_pagetable_t *curr_pt        = arch_mmu_get_table();
    void                 *fault_virt     = (void *)ctx->cr2;
    uint64_t              entry          = *get_page(curr_pt, fault_virt);
    void                 *fault_phys     = (void *)(entry & ADDRMASK);
    arch_mmu_flags_t      mmu_flags_hint = entry & ~ADDRMASK;

    int vmm_attr = 0;
    if (ARCH_MMU_EXTRA_FLAGS_PRIVATE & entry) {
        mmu_flags_hint &= ~ARCH_MMU_EXTRA_FLAGS_PRIVATE;
        mmu_flags_hint |= ARCH_MMU_FLAGS_WRITE;
        vmm_attr |= COM_MM_VMM_FAULT_ATTR_COW;
    }
    if (ARCH_MMU_EXTRA_FLAGS_NOCOPY & entry) {
        // We still want the vmm to map, but we don't want to copy the zero page
        // into a page taht's laready been zeroed. So we remove COW (copy on
        // write) and add MAP
        mmu_flags_hint &= ~ARCH_MMU_EXTRA_FLAGS_NOCOPY;
        vmm_attr &= ~COM_MM_VMM_FAULT_ATTR_COW;
        vmm_attr |= COM_MM_VMM_FAULT_ATTR_MAP;
    }

    com_mm_vmm_handle_fault(fault_virt,
                            fault_phys,
                            ctx,
                            mmu_flags_hint,
                            vmm_attr);
}

void x86_64_mmu_invalidate_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
    (void)ctx;

    arch_cpu_t *curr_cpu = ARCH_CPU_GET();
    kspinlock_fake_acquire(); // acquired by sender
    internal_invalidate(curr_cpu->mmu_shootdown_virt,
                        curr_cpu->mmu_shootdown_pages);
    KASSERT(KSPINLOCK_IS_HELD(&curr_cpu->mmu_shootdown_lock));
    __atomic_add_fetch(curr_cpu->mmu_shootdown_counter, 1, __ATOMIC_SEQ_CST);
    kspinlock_release(&curr_cpu->mmu_shootdown_lock); // realeased by us
}
