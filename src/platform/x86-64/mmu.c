#include <arch/cpu.h>
#include <arch/info.h>
#include <arch/mmu.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/pmmcache.h>
#include <kernel/com/util.h>
#include <kernel/platform/info.h>
#include <kernel/platform/mmu.h>
#include <kernel/platform/x86-64/mmu.h>
#include <kernel/platform/x86-64/smp.h>
#include <threads.h>

#define PT_CACHE_INIT_SIZE     1024
#define PT_CACHE_POOL_SIZE     512
#define PT_CACHE_MAX_POOL_SIZE 4096

#define ADDRMASK (u64)0x7ffffffffffff000
#define PTMASK   (u64)0b111111111000000000000
#define PDMASK   (u64)0b111111111000000000000000000000
#define PDPTMASK (u64)0b111111111000000000000000000000000000000
#define PML4MASK (u64)0b111111111000000000000000000000000000000000000000

#define DEPTH_PDPT 1
#define DEPTH_PD   2
#define DEPTH_PT   3

extern u8 _TEXT_START[];
extern u8 _TEXT_END[];
extern u8 _RODATA_START[];
extern u8 _RODATA_END[];
extern u8 _DATA_START[];
extern u8 _DATA_END[];
extern u8 _USER_TEXT_START[];
extern u8 _USER_TEXT_END[];
extern u8 _USER_DATA_START[];
extern u8 _USER_DATA_END[];

static MMUPageTable *g_root_table = NULL;

static inline void *internal_alloc_phys(void) {
    Cpu *curr_cpu = cpu_get();
    return pmm_cache_alloc(&curr_cpu->mmu_cache, 1);
}

static inline void internal_free_phys(void *page) {
    Cpu *curr_cpu = cpu_get();
    pmm_cache_free(&curr_cpu->mmu_cache, page);
}

static inline u64 *next(u64 entry) {
    if (entry == 0) return NULL;

    return (u64 *)PHYS_TO_HHDM(entry & ADDRMASK);
}

static u64 *get_page(MMUPageTable *top, void *vaddr) {
    u64 *pml4 = (u64 *)PHYS_TO_HHDM(top);
    uptr addr = (uptr)vaddr;
    uptr ptoffset = (addr & PTMASK) >> 12;
    uptr pdoffset = (addr & PDMASK) >> 21;
    uptr pdptoffset = (addr & PDPTMASK) >> 30;
    uptr pml4offset = (addr & PML4MASK) >> 39;

    u64 *pdpt = next(pml4[pml4offset]);
    if (pdpt == NULL) return NULL;

    u64 *pd = next(pdpt[pdptoffset]);
    if (pd == NULL) return NULL;

    u64 *pt = next(pd[pdoffset]);
    if (pt == NULL) return NULL;

    return pt + ptoffset;
}

static bool add_page(MMUPageTable *top, void *vaddr, u64 entry, int depth) {
    u64 *pml4 = (u64 *)PHYS_TO_HHDM(top);
    uptr addr = (uptr)vaddr;
    uptr ptoffset = (addr & PTMASK) >> 12;
    uptr pdoffset = (addr & PDMASK) >> 21;
    uptr pdptoffset = (addr & PDPTMASK) >> 30;
    uptr pml4offset = (addr & PML4MASK) >> 39;

    if (depth == DEPTH_PDPT) {
        pml4[pml4offset] = entry;
        return true;
    }

    u64 *pdpt = next(pml4[pml4offset]);
    if (pdpt == NULL) {
        pdpt = internal_alloc_phys();
        if (pdpt == NULL) return false;
        pml4[pml4offset] = (u64)pdpt | MMU_FLAGS_READ | MMU_FLAGS_WRITE |
            MMU_FLAGS_USER;
        pdpt = (u64 *)PHYS_TO_HHDM(pdpt);
    }

    if (depth == DEPTH_PD) {
        pdpt[pdptoffset] = entry;
        return true;
    }

    u64 *pd = next(pdpt[pdptoffset]);
    if (pd == NULL) {
        pd = internal_alloc_phys();
        if (pd == NULL) return false;
        pdpt[pdptoffset] = (u64)pd | MMU_FLAGS_READ | MMU_FLAGS_WRITE |
            MMU_FLAGS_USER;
        pd = (u64 *)PHYS_TO_HHDM(pd);
    }

    if (depth == DEPTH_PT) {
        pd[pdoffset] = entry;
        return true;
    }

    u64 *pt = next(pd[pdoffset]);
    if (pt == NULL) {
        pt = internal_alloc_phys();
        if (pt == NULL) return false;
        pd[pdoffset] = (u64)pt | MMU_FLAGS_READ | MMU_FLAGS_WRITE |
            MMU_FLAGS_USER;
        pt = (u64 *)PHYS_TO_HHDM(pt);
    }

    pt[ptoffset] = entry;
    return true;
}

// This must be called with the guarantee that the target of the TLB shootdown
// is the current Cpu. i.e., only if curr_pt == shootdown_pt
static inline void internal_invalidate(void *virt, usize pages) {
    if (virt == NULL || pages > 32) {
        // this puts the current cr3 back into cr3 which causes a full TLB
        // shootdown
        asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3;"
                     :
                     :
                     : "rax", "memory");
        return;
    }

    // If we have a smaller invalidation, it is faster to perform invlpg
    for (usize i = 0; i < pages; i++) {
        uptr ptr = (uptr)virt + i * PAGE_SIZE;
        asm volatile("invlpg (%%rax)" : : "a"(ptr) : "memory");
    }
}

// CREDIT: vloxei64/ke
static u64 duplicate_recursive(u64 entry, usize level, usize addr) {
    u64 *virt = (u64 *)PHYS_TO_HHDM(entry & ADDRMASK);
    bool entry_shared = entry & MMU_EXTRA_FLAGS_SHARED;
    bool entry_writable = entry & MMU_FLAGS_WRITE;

    if (!(entry & MMU_FLAGS_PRESENT)) return 0;

    if (level == 0) {
        pmm_hold((void *)(entry & ADDRMASK));
        return (entry_shared)
            ? entry
            : (entry & ~MMU_FLAGS_WRITE) | (entry_writable << 9);
    }

    u64 new = (u64)internal_alloc_phys();
    u64 *nvirt = (u64 *)PHYS_TO_HHDM(new);

    for (usize i = 0; i < 512; i++) {
        if (virt[i] & MMU_FLAGS_PRESENT) {
            nvirt[i] = duplicate_recursive(
                virt[i], level - 1, addr | ((i << (12 + (level - 1) * 9))));
            if (level == 1) virt[i] = nvirt[i];
        }
    }

    return new | (entry & ~ADDRMASK);
}

void destroy_recursive(u64 entry, usize level) {
    u64 *directory = (void *)PHYS_TO_HHDM(entry & ADDRMASK);
    void *phys = (void *)(entry & ADDRMASK);

    if (level != 0) {
        for (usize i = 0; i < 512; i++) {
            if (directory[i] & MMU_FLAGS_PRESENT) {
                destroy_recursive(directory[i], level - 1);
            }
        }

        internal_free_phys(phys);
    } else {
        // If we're at level 0, then this page is not part of the PT and needs
        // to be returned to pmm
        pmm_free(phys);
    }
}

MMUPageTable *mmu_new_table(void) {
    MMUPageTable *table = internal_alloc_phys();

    if (table == NULL) return NULL;

    memcpy((void *)PHYS_TO_HHDM(table + 256), g_root_table + 256,
           PAGE_SIZE / 2);
    return table;
}

void mmu_destroy_table(MMUPageTable *pt) {
    MMUPageTable *pt_virt = (void *)PHYS_TO_HHDM(pt);

    for (usize i = 0; i < 256; i++) {
        if (pt_virt[i] & MMU_FLAGS_PRESENT) destroy_recursive(pt_virt[i], 3);
    }

    internal_free_phys(pt);
}

MMUPageTable *mmu_duplicate_table(MMUPageTable *pt) {
    MMUPageTable *new_pt = mmu_new_table();

    if (new_pt == NULL) return NULL;

    MMUPageTable *new_virt = (MMUPageTable *)PHYS_TO_HHDM(new_pt);
    MMUPageTable *pt_virt = (MMUPageTable *)PHYS_TO_HHDM(pt);

    for (usize i = 0; i < 256; i++) {
        if (pt_virt[i] & MMU_FLAGS_PRESENT) {
            new_virt[i] = duplicate_recursive(pt_virt[i], 3, 0);
        }
    }

    return new_pt;
}

bool mmu_map(MMUPageTable *pt, void *virt, void *phys, MMUFlags flags) {
    return add_page(pt, virt, ((u64)phys & ADDRMASK) | flags, 0);
}

bool mmu_chflags(MMUPageTable *pt, void *virt, MMUFlags new_flags) {
    u64 *entry = get_page(pt, virt);
    if (entry == NULL) return false;
    *entry = (*entry & ADDRMASK) | new_flags;
    return true;
}

bool mmu_unmap(MMUPageTable *pt, void *virt, void **out_old_phys) {
    u64 *entry = get_page(pt, virt);
    if (entry == NULL) return false;
    *out_old_phys = (void *)((*entry) & ADDRMASK);
    *entry = 0;
    return true;
}

// CREDIT: Mathewnd/Astral
void mmu_invalidate(MMUPageTable *pt, void *virt, usize pages) {
    Thread *curr_thread = cpu_get_thread();
    Cpu *curr_cpu = cpu_get();
    MMUPageTable *curr_pt = mmu_get_table();

    // as of kernel 0.2.4, NULL is still almost always a legal userspace
    // addres since we don't have ELF slide working properly, so that check
    // is redundant, but we'll keep it just in case.
    bool is_user = virt > MMU_USERSPACE_START &&
        (virt + pages * PAGE_SIZE) < MMU_KERNELSPACE_START;
    bool do_shootdown = curr_thread != NULL &&
        (virt >= MMU_KERNELSPACE_START ||
         ((virt == NULL || is_user) && curr_thread->proc != NULL &&
          __atomic_load_n(&curr_thread->proc->num_running_threads,
                          __ATOMIC_ACQUIRE) > 1));
    // numer of other CPUs that have completed the shootdown
    usize shootdown_counter = 0;
    // expected number of CPUs to compelte the shootdown
    usize num_other_cpus = 0;

    Cpu *next_cpu;
    for (usize i = 0; do_shootdown && (next_cpu = smp_get_cpu(i)) != NULL;
         i++) {
        // Skipp current Cpu, we'll do that later
        if (next_cpu->id == curr_cpu->id) continue;
        // Skip CPUs that are not running any process or are running a different
        // process then the one we're targetting
        spinlock_acq(&next_cpu->runqueue_lock);
        Thread *next_cpu_thread = next_cpu->thread;
        if (next_cpu_thread == NULL || next_cpu_thread->proc == NULL ||
            pt != next_cpu_thread->proc->vmm_context->pagetable) {
            spinlock_rel(&next_cpu->runqueue_lock);
            continue;
        }
        spinlock_rel(&next_cpu->runqueue_lock);

        // Now here we know we have ourselves a Cpu we can target. At the very
        // worst it will context switch and receivea spurious IPI and we loose a
        // little bit of performance
        spinlock_acq(&next_cpu->mmu_shootdown_lock);
        next_cpu->mmu_shootdown_counter = &shootdown_counter;
        next_cpu->mmu_shootdown_virt = virt;
        next_cpu->mmu_shootdown_pages = pages;
        num_other_cpus++;
        cpu_send_ipi(next_cpu, MMU_IPI_INVALIDATE);

        // lock released by InterruptHandler
        spinlock_fake_rel();
    }

    // If we also have to perform a shootdown
    if (pt == curr_pt) internal_invalidate(virt, pages);

    // And finally, we wait for all others to complete
    while (do_shootdown &&
           __atomic_load_n(&shootdown_counter, __ATOMIC_ACQUIRE) !=
               num_other_cpus) {
        cpu_pause();
    }
}

void mmu_switch(MMUPageTable *pt) {
    // log_debug("switching to page table at address %p", pt);
    asm volatile("mov %%rax, %%cr3" : : "a"(pt));
}

void mmu_switch_default(void) {
    mmu_switch((void *)HHDM_TO_PHYS(g_root_table));
}

static int mapping_info(u64 entry, MMUFlags *out_flags) {
    MMUFlags flags = entry & ~ADDRMASK;
    int attr = 0;
    if ((entry & MMU_FLAGS_PRESENT) && (entry & MMU_EXTRA_FLAGS_PRIVATE)) {
        flags &= ~(MMU_EXTRA_FLAGS_PRIVATE | MMU_EXTRA_FLAGS_NOCOPY);
        flags |= MMU_FLAGS_WRITE;
        attr = (entry & MMU_EXTRA_FLAGS_NOCOPY) ? VMM_FAULT_ATTR_MAP
                                                : VMM_FAULT_ATTR_COW;
    }
    if (out_flags != NULL) *out_flags = flags;
    return attr;
}

void *mmu_get_physical(MMUPageTable *pagetable, void *virt_addr,
                       MMUFlags *out_flags, int *out_attr) {
    u64 *entry = get_page(pagetable, virt_addr);
    u64 value = entry != NULL ? *entry : 0;
    int attr = mapping_info(value, out_flags);
    if (out_attr != NULL) *out_attr = attr;
    if (!(value & MMU_FLAGS_PRESENT)) return NULL;
    return (void *)((value & ADDRMASK) + ((uptr)virt_addr & (PAGE_SIZE - 1)));
}

bool mmu_is_cow(MMUPageTable *pagetable, void *virt_addr) {
    u64 *entry = get_page(pagetable, virt_addr);
    if (entry == NULL) return false;
    return *entry & MMU_EXTRA_FLAGS_PRIVATE;
}

bool mmu_is_executable(MMUPageTable *pagetable, void *virt_addr) {
    u64 *entry = get_page(pagetable, virt_addr);
    if (entry == NULL) return false;
    return !(*entry & MMU_FLAGS_NOEXEC);
}

MMUPageTable *mmu_get_table(void) {
    void *pt;
    asm volatile("mov %%cr3, %0" : "=r"(pt));
    return pt;
}

void mmu_init(void) {
    // Allocate the kernel (root) page table
    log("initializing mmu");
    mmu_init_cpu();
    g_root_table = internal_alloc_phys();
    assert(g_root_table != NULL);
    g_root_table = (MMUPageTable *)PHYS_TO_HHDM(g_root_table);

    // Map the higher half into the new page table
    log_debug("mapping higher half to kernel page table");
    for (u64 i = 256; i < 512; i++) {
        u64 *entry = internal_alloc_phys();
        assert(entry != NULL);
        g_root_table[i] = (u64)entry | MMU_FLAGS_WRITE | MMU_FLAGS_READ |
            MMU_FLAGS_GLOBAL;
    }

    // Mapp all the necessary memmap entries
    MemMap *memmap = info_get_memmap();
    for (u64 i = 0; i < memmap->entry_count; i++) {
        MemMapEntry *entry = memmap->entries[i];

        if (memmap_is_mappable(entry)) {
            log_debug("mapping page table entry range %p -> %p", entry->base,
                      entry->base + entry->length);

            MMUFlags other_flags = 0;
            if (memmap_is_framebuffer(entry)) other_flags |= MMU_FLAGS_WC;

            for (u64 i = 0; i < entry->length; i += PAGE_SIZE) {
                u64 pt_entry = ((entry->base + i) & ADDRMASK) | MMU_FLAGS_READ |
                    MMU_FLAGS_WRITE | MMU_FLAGS_NOEXEC | other_flags;
                verify(add_page((MMUPageTable *)HHDM_TO_PHYS(g_root_table),
                                (u64 *)PHYS_TO_HHDM(entry->base + i), pt_entry,
                                0));
            }
        }
    }

    // Map the kernel itself (using image map)
    log_debug("mapping kernel image to page table");
    KAddr *kaddr = info_get_kaddr();
    u64 vp_delta = kaddr->virtual_base - kaddr->physical_base;
    log_debug("kernel virt base: %p, kernel phys base: %p, delta: %p",
              kaddr->virtual_base, kaddr->physical_base, vp_delta);

    log_debug("mapping kernel text section (virtual: %p -> %p)", _TEXT_START,
              _TEXT_END);
    for (uptr i = (uptr)_TEXT_START; i < (uptr)_TEXT_END; i += PAGE_SIZE) {
        verify(add_page((MMUPageTable *)HHDM_TO_PHYS(g_root_table), (u64 *)i,
                        ((i - vp_delta) & ADDRMASK) | MMU_FLAGS_READ |
                            MMU_FLAGS_GLOBAL,
                        0));
    }

    log_debug("mapping kernel rodata section (virtual: %p -> %p)",
              _RODATA_START, _RODATA_END);
    for (uptr i = (uptr)_RODATA_START; i < (uptr)_RODATA_END; i += PAGE_SIZE) {
        verify(add_page((MMUPageTable *)HHDM_TO_PHYS(g_root_table), (u64 *)i,
                        ((i - vp_delta) & ADDRMASK) | MMU_FLAGS_READ |
                            MMU_FLAGS_NOEXEC | MMU_FLAGS_GLOBAL,
                        0));
    }

    log_debug(
        "mapping kernel data/bss/limine_requests sections (virtual: %p -> %p)",
        _DATA_START, _DATA_END);
    for (uptr i = (uptr)_DATA_START; i < (uptr)_DATA_END; i += PAGE_SIZE) {
        verify(add_page((MMUPageTable *)HHDM_TO_PHYS(g_root_table), (u64 *)i,
                        ((i - vp_delta) & ADDRMASK) | MMU_FLAGS_READ |
                            MMU_FLAGS_WRITE | MMU_FLAGS_NOEXEC |
                            MMU_FLAGS_GLOBAL,
                        0));
    }

    mmu_switch((MMUPageTable *)HHDM_TO_PHYS(g_root_table));
    cpu_get()->root_page_table = (void *)HHDM_TO_PHYS(g_root_table);
}

void mmu_init_cpu(void) {
    Cpu *curr_cpu = cpu_get();
    if (curr_cpu->mmu_cache.private.pool != NULL) return;

    pmm_cache_init(&curr_cpu->mmu_cache, PT_CACHE_INIT_SIZE, PT_CACHE_POOL_SIZE,
                   PT_CACHE_MAX_POOL_SIZE,
                   PMM_CACHE_FLAGS_AUTOALLOC | PMM_CACHE_FLAGS_AUTOLOCK);
}

void mmu_fault_isr(InterruptHandler *isr, Context *ctx) {
    (void)isr;

    MMUPageTable *curr_pt = mmu_get_table();
    void *fault_virt = (void *)ctx->cr2;
    u64 *entry_ptr = get_page(curr_pt, fault_virt);
    u64 entry = entry_ptr != NULL ? *entry_ptr : 0;
    u64 start_entry = entry;
    void *start_virt = fault_virt;
    MMUFlags mmu_flags = entry & ~ADDRMASK;
    MMUFlags mmu_flags_hint = mmu_flags;
    usize num_pages = 1;

    int vmm_attr = mapping_info(entry, &mmu_flags_hint);

    if (!(vmm_attr & VMM_PREFAULT_SUPPORTED_ATTR)) goto call_vmm;

    for (; num_pages < CONFIG_VMM_PREFAULT_MAX / 2; num_pages++) {
        u64 *prev_entry_tr = get_page(
            curr_pt, (char *)fault_virt - PAGE_SIZE * num_pages);
        if (prev_entry_tr == NULL) break;
        u64 prev_entry = *prev_entry_tr;
        MMUFlags prev_mmu_flags = prev_entry & ~ADDRMASK;
        if (mmu_flags != prev_mmu_flags) break;
        start_entry = prev_entry;
        start_virt = (void *)((uptr)start_virt - PAGE_SIZE);
    }

    for (usize i = 1; num_pages < CONFIG_VMM_PREFAULT_MAX; i++, num_pages++) {
        u64 *next_entry = get_page(curr_pt, (char *)fault_virt + PAGE_SIZE * i);
        if (next_entry == NULL) break;
        MMUFlags next_mmu_flags = *next_entry & ~ADDRMASK;
        if (mmu_flags != next_mmu_flags) break;
    }

call_vmm:
    vmm_handle_fault(start_virt, (void *)(start_entry & ADDRMASK), ctx,
                     mmu_flags_hint, vmm_attr, num_pages);
}

void mmu_invalidate_isr(InterruptHandler *isr, Context *ctx) {
    (void)isr;
    (void)ctx;

    Cpu *curr_cpu = cpu_get();
    spinlock_fake_acq(); // acquired by sender
    internal_invalidate(curr_cpu->mmu_shootdown_virt,
                        curr_cpu->mmu_shootdown_pages);
    assert(spinlock_is_held(&curr_cpu->mmu_shootdown_lock));
    __atomic_add_fetch(curr_cpu->mmu_shootdown_counter, 1, __ATOMIC_RELAXED);
    spinlock_rel(&curr_cpu->mmu_shootdown_lock); // realeased by us
}
