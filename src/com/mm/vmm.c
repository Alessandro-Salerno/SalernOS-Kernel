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
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/mmu.h>
#include <lib/str.h>
#include <lib/sync.h>
#include <lib/usercopy.h>
#include <stdatomic.h>

#define UNMAP_MAX_ARRAY_ON_STACK 64

static com_vmm_context_t RootContext = {0};
static void             *ZeroPage    = NULL;

static inline com_vmm_context_t *vmm_current_context(void) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL == curr_thread || NULL == curr_thread->proc ||
        NULL == curr_thread->proc->vmm_context) {
        return &RootContext;
    }
    return curr_thread->proc->vmm_context;
}

static inline com_vmm_context_t *
vmm_ensure_context(com_vmm_context_t *context) {
    KASSERT(&RootContext != context);

    if (NULL == context) {
        return vmm_current_context();
    }

    return context;
}

com_vmm_context_t *com_mm_vmm_new_context(arch_mmu_pagetable_t *pagetable) {
    if (NULL == pagetable) {
        pagetable = arch_mmu_new_table();
    }

    com_vmm_context_t *context = com_mm_slab_alloc(sizeof(com_vmm_context_t));
    context->pagetable         = pagetable;
    context->lock              = KSPINLOCK_NEW();
    return context;
}

void com_mm_vmm_destroy_context(com_vmm_context_t *context) {
    KASSERT(NULL != context);
    KASSERT(&RootContext != context);
    KASSERT(RootContext.pagetable != context->pagetable);
    KASSERT(context != vmm_current_context());
    KASSERT(context->pagetable != arch_mmu_get_table());
    KASSERT(NULL != context->pagetable);

    // TODO: reimplement this
}

com_vmm_context_t *com_mm_vmm_duplicate_context(com_vmm_context_t *context) {
    if (NULL == context) {
        return com_mm_vmm_new_context(NULL);
    }

    arch_mmu_pagetable_t *new_pt = arch_mmu_duplicate_table(context->pagetable);
    com_vmm_context_t    *new_vmm_ctx = com_mm_vmm_new_context(new_pt);
    new_vmm_ctx->anon_pages           = context->anon_pages;
    return new_vmm_ctx;
}

static void *vmm_alloc_anon_range(com_vmm_context_t *context, size_t pages) {
    KASSERT(&RootContext != context);
    KASSERT(RootContext.pagetable != context->pagetable);

    size_t max_pages = ((uintptr_t)ARCH_MMU_USERSPACE_END -
                        CONFIG_VMM_ANON_START) /
                       ARCH_PAGE_SIZE;
    void *base = NULL;

    kspinlock_acquire(&context->lock);
    if (pages <= max_pages - context->anon_pages) {
        base = (void *)(context->anon_pages * ARCH_PAGE_SIZE +
                        CONFIG_VMM_ANON_START);
        context->anon_pages += pages;
    }
    kspinlock_release(&context->lock);

    return base;
}

void *com_mm_vmm_map(com_vmm_context_t *context,
                     void              *virt,
                     void              *phys,
                     size_t             len,
                     int                vmm_flags,
                     arch_mmu_flags_t   mmu_flags) {
    if (COM_MM_VMM_FLAGS_ALLOCATE & vmm_flags) {
        phys = NULL;
    }

#ifdef ARCH_MMU_EXTRA_FLAGS_SHARED
    if (COM_MM_VMM_FLAGS_SHARED & vmm_flags) {
        mmu_flags |= ARCH_MMU_EXTRA_FLAGS_SHARED;
    }
#else
#warning "no support for architectures without ARCH_MMU_EXTRA_FLAGS_SHARED yet"
#endif

    if (COM_MM_VMM_FLAGS_ALLOCATE & vmm_flags) {
        vmm_flags |= COM_MM_VMM_FLAGS_PRIVATE;
#ifdef ARCH_MMU_EXTRA_FLAGS_NOCOPY
        mmu_flags |= ARCH_MMU_EXTRA_FLAGS_NOCOPY;
#else
#warning "no support for architectures without ARCH_MMU_EXTRA_FLAGS_NOCOPY yet"
#endif
    }

#ifdef ARCH_MMU_EXTRA_FLAGS_PRIVATE
    if (COM_MM_VMM_FLAGS_PRIVATE & vmm_flags &&
        ARCH_MMU_FLAGS_WRITE & mmu_flags) {
        mmu_flags &= ~ARCH_MMU_FLAGS_WRITE;
        mmu_flags |= ARCH_MMU_EXTRA_FLAGS_PRIVATE;
    }
#else
#warning "no support for architectures without ARCH_MMU_EXTRA_FLAGS_PRIVATE yet"
#endif

    void     *page_paddr = (void *)((uintptr_t)phys &
                                ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
    uintptr_t page_off   = (uintptr_t)phys % ARCH_PAGE_SIZE;
    size_t    size_p_off = page_off + len;
    size_t size_in_pages = (size_p_off + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;

    context = vmm_ensure_context(context);

    if (COM_MM_VMM_FLAGS_NOHINT & vmm_flags) {
        if (COM_MM_VMM_FLAGS_ANONYMOUS & vmm_flags) {
            virt = vmm_alloc_anon_range(context, size_in_pages);
            if (NULL == virt) {
                return NULL;
            }
        } else if (COM_MM_VMM_FLAGS_PHYSICAL & vmm_flags) {
            virt = (void *)ARCH_PHYS_TO_HHDM(page_paddr);
            mmu_flags |= ARCH_MMU_FLAGS_GLOBAL;
        } else {
            KASSERT(!"unsupported vmm flags");
        }
    } else {
        virt = (void *)((uintptr_t)virt & ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
    }

    for (size_t i = 0; i < size_in_pages; i++) {
        uintptr_t offset = i * ARCH_PAGE_SIZE;
        void     *vaddr  = (void *)((uintptr_t)virt + offset);

        void *paddr = (void *)((uintptr_t)page_paddr + offset);
        if (COM_MM_VMM_FLAGS_ALLOCATE & vmm_flags) {
            paddr = ZeroPage;
            com_mm_pmm_hold(ZeroPage);
        }

        bool success = arch_mmu_map(context->pagetable,
                                    vaddr,
                                    paddr,
                                    mmu_flags);
        KASSERT(success);
    }

    return (void *)((uintptr_t)virt + page_off);
}

void com_mm_vmm_switch(com_vmm_context_t *context) {
    if (NULL == context) {
        context = &RootContext;
    }

    arch_mmu_switch(context->pagetable);
}

void *com_mm_vmm_get_physical(com_vmm_context_t *context, void *virt_addr) {
    context = vmm_ensure_context(context);
    arch_mmu_flags_t flags;
    int              attr;
    void            *phys = arch_mmu_get_physical(&flags,
                                       &attr,
                                       context->pagetable,
                                       virt_addr);
    if (NULL == phys ||
        !(attr & (COM_MM_VMM_FAULT_ATTR_MAP | COM_MM_VMM_FAULT_ATTR_COW))) {
        return phys;
    }

    uintptr_t offset    = (uintptr_t)virt_addr & (ARCH_PAGE_SIZE - 1);
    void     *old_page  = (void *)((uintptr_t)phys - offset);
    void     *virt_page = (void *)((uintptr_t)virt_addr - offset);
    void     *new_page  = (attr & COM_MM_VMM_FAULT_ATTR_MAP)
                              ? com_mm_pmm_alloc_zero()
                              : com_mm_pmm_alloc();
    if (NULL == new_page) {
        return NULL;
    }
    if (attr & COM_MM_VMM_FAULT_ATTR_COW) {
        kmemcpy((void *)ARCH_PHYS_TO_HHDM(new_page),
                (void *)ARCH_PHYS_TO_HHDM(old_page),
                ARCH_PAGE_SIZE);
    }
    if (!arch_mmu_map(context->pagetable, virt_page, new_page, flags)) {
        com_mm_pmm_free(new_page);
        return NULL;
    }
    arch_mmu_invalidate(context->pagetable, virt_page, 1);
    com_mm_pmm_free(old_page);
    return (void *)((uintptr_t)new_page + offset);
}

void com_mm_vmm_unmap(com_vmm_context_t *context, void *virt, size_t len) {
    context = vmm_ensure_context(context);

    uintptr_t page_off   = (uintptr_t)virt % ARCH_PAGE_SIZE;
    size_t    size_p_off = page_off + len;
    size_t size_in_pages = (size_p_off + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
    virt = (void *)((uintptr_t)virt & ~(uintptr_t)(ARCH_PAGE_SIZE - 1));

    void  *tmp_phys_array[UNMAP_MAX_ARRAY_ON_STACK];
    void **tmp_phys_buf       = tmp_phys_array;
    size_t tmp_phys_buf_pages = (size_in_pages * sizeof(void *) +
                                 ARCH_PAGE_SIZE - 1) /
                                ARCH_PAGE_SIZE;
    size_t unmaps_done = 0;
    if (size_in_pages > UNMAP_MAX_ARRAY_ON_STACK) {
        tmp_phys_buf = (void *)ARCH_PHYS_TO_HHDM(
            com_mm_pmm_alloc_many(tmp_phys_buf_pages));
    }

    for (size_t i = 0; i < size_in_pages; i++) {
        size_t offset = i * ARCH_PAGE_SIZE;
        if (!arch_mmu_unmap(&tmp_phys_buf[i],
                            context->pagetable,
                            virt + offset)) {
            tmp_phys_buf[i] = NULL;
            break;
        }
        unmaps_done++;
    }

    arch_mmu_invalidate(context->pagetable, virt, unmaps_done);

    for (size_t i = 0; i < unmaps_done; i++) {
        com_mm_pmm_free(tmp_phys_buf[i]);
    }

    if (size_in_pages > UNMAP_MAX_ARRAY_ON_STACK) {
        com_mm_pmm_free_many((void *)ARCH_HHDM_TO_PHYS(tmp_phys_buf),
                             tmp_phys_buf_pages);
    }
}

// NOTE: always runs in thread context with interrupts on, unless a kernel bug
// caused it, in which case the state is unknown
void com_mm_vmm_handle_fault(void            *fault_virt,
                             void            *fault_phys,
                             arch_context_t  *fault_ctx,
                             arch_mmu_flags_t mmu_flags_hint,
                             int              attr,
                             size_t           num_pages_hint) {
    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_VMM_HANDLE_FAULT);
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    if (KUNKLIKELY(NULL == curr_thread)) {
        com_sys_panic(fault_ctx,
                      "unrecoverable page fault in kernel setup code on "
                      "address (virt = %p, phys = %p)",
                      fault_virt,
                      fault_phys);
    }

    com_proc_t *curr_proc  = curr_thread->proc;
    bool        is_user    = ARCH_CONTEXT_ISUSER(&curr_thread->ctx);
    bool        thread_ctx = ARCH_CONTEXT_ISUSER(fault_ctx) ||
                      ARCH_MMU_ADDR_IS_USER(fault_virt);
    if (KUNKLIKELY(!thread_ctx)) {
        goto kernel_fault;
    }

    // Spinlocks disable interrupts, so this also catches faults under them
    if (KUNKLIKELY(!ARCH_CONTEXT_INTSTATUS(fault_ctx))) {
        com_sys_panic(fault_ctx,
                      "page fault on user memory with interrupts disabled "
                      "(virt = %p)",
                      fault_virt);
    }
    KASSERT(1 == curr_thread->lock_depth);
    curr_thread->lock_depth = 0;
    ARCH_CPU_ENABLE_INTERRUPTS();

    if (KUNKLIKELY(!is_user || NULL == curr_proc)) {
        goto kernel_thread;
    }

    if (KLIKELY(COM_MM_VMM_FAULT_ATTR_MAP & attr)) {
        void *fault_virt_page = (void *)((uintptr_t)fault_virt &
                                         ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
        for (size_t i = 0; i < num_pages_hint;) {
            size_t got;
            void  *new_phys = com_mm_pmm_alloc_max_zero(&got,
                                                        num_pages_hint - i);
            for (size_t j = 0; j < got; j++, i++) {
                arch_mmu_map(curr_proc->vmm_context->pagetable,
                             fault_virt_page + ARCH_PAGE_SIZE * i,
                             new_phys + ARCH_PAGE_SIZE * j,
                             mmu_flags_hint);
            }
        }
        arch_mmu_invalidate(curr_proc->vmm_context->pagetable,
                            fault_virt_page,
                            num_pages_hint);
        // com_mm_pmm_free(ZeroPage);
        goto end;
    }

    if (KLIKELY(COM_MM_VMM_FAULT_ATTR_COW & attr)) {
        void *fault_virt_page = (void *)((uintptr_t)fault_virt &
                                         ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
        void *fault_phys_page = (void *)((uintptr_t)fault_phys &
                                         ~(uintptr_t)(ARCH_PAGE_SIZE - 1));
        if (!com_mm_pmm_is_shared(fault_phys_page)) {
            arch_mmu_map(curr_proc->vmm_context->pagetable,
                         fault_virt_page,
                         fault_phys_page,
                         mmu_flags_hint);
            arch_mmu_invalidate(curr_proc->vmm_context->pagetable,
                                fault_virt_page,
                                1);
            goto end;
        }
        void *fault_hhdm = (void *)ARCH_PHYS_TO_HHDM(fault_phys_page);
        void *new_phys   = (void *)com_mm_pmm_alloc();
        void *new_hhdm   = (void *)ARCH_PHYS_TO_HHDM(new_phys);
        kmemcpy(new_hhdm, fault_hhdm, ARCH_PAGE_SIZE);
        arch_mmu_map(curr_proc->vmm_context->pagetable,
                     fault_virt_page,
                     new_phys,
                     mmu_flags_hint);
        arch_mmu_invalidate(curr_proc->vmm_context->pagetable,
                            fault_virt_page,
                            1);
        com_mm_pmm_free(fault_phys_page);
        goto end;
    }

    if (KLIKELY(ARCH_CONTEXT_ISUSER(fault_ctx))) {
        com_ipc_signal_send_to_proc(curr_proc->pid, SIGSEGV, NULL);
        goto end;
    }

kernel_thread:
    if (kusercopy_fixup(fault_ctx)) {
        goto end;
    }

kernel_fault:

    com_sys_panic(fault_ctx,
                  "unrecoverable page fault by %s thread (tid = %d) on "
                  "address (virt = %p, phys = %p) in %s context",
                  (is_user) ? "USER" : "KERNEL",
                  curr_thread->tid,
                  fault_virt,
                  fault_phys,
                  ARCH_CONTEXT_ISUSER(fault_ctx) ? "USER" : "KERNEL");

end:
    if (thread_ctx) {
        ARCH_CPU_DISABLE_INTERRUPTS();
        curr_thread->lock_depth = 1;
    }
    com_sys_profiler_end_function(&profiler_data);
}

void com_mm_vmm_init(void) {
    KLOG("initializing vmm");
    RootContext.pagetable = arch_mmu_get_table();
    RootContext.lock      = KSPINLOCK_NEW();
    ZeroPage              = com_mm_pmm_alloc_zero();
}

void *com_mm_vmm_prealloc_range(com_vmm_context_t   *context,
                                com_vmm_range_type_t rangetype,
                                size_t               len) {
    (void)rangetype;
    context = vmm_ensure_context(context);
    return vmm_alloc_anon_range(context,
                                len / ARCH_PAGE_SIZE +
                                    (0 != len % ARCH_PAGE_SIZE));
}

void com_mm_vmm_init_reaper(void) {
    KLOG("TODO: reimplement vmm reaper");
}
