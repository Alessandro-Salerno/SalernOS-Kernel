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
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/thread.h>

static com_vmm_context_t RootContext = {0};

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
    if (NULL == context) {
        return vmm_current_context();
    }

    return context;
}

com_vmm_context_t *com_mm_vmm_new_context(arch_mmu_pagetable_t *pagetable) {
    com_vmm_context_t *context = com_mm_slab_alloc(sizeof(com_vmm_context_t));
    context->pagetable         = pagetable;
    context->lock              = KSPINLOCK_NEW();
    return context;
}

void com_mm_vmm_destroy_context(com_vmm_context_t *context) {
    arch_mmu_destroy_table(context->pagetable);
    com_mm_slab_free(context, sizeof(com_vmm_context_t));
}

com_vmm_context_t *com_mm_vmm_duplicate_context(com_vmm_context_t *context) {
    context                      = vmm_ensure_context(context);
    arch_mmu_pagetable_t *new_pt = arch_mmu_duplicate_table(context->pagetable);
    com_vmm_context_t    *new_vmm_ctx = com_mm_vmm_new_context(new_pt);
    new_vmm_ctx->anon_pages           = context->anon_pages;
    return new_vmm_ctx;
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

    void     *page_paddr = (void *)((uintptr_t)phys & ~(ARCH_PAGE_SIZE - 1));
    uintptr_t page_off   = (uintptr_t)phys % ARCH_PAGE_SIZE;
    size_t    size_p_off = page_off + len;
    size_t size_in_pages = (size_p_off + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;

    context = vmm_ensure_context(context);

    if (COM_MM_VMM_FLAGS_NOHINT & vmm_flags) {
        if (COM_MM_VMM_FLAGS_ANONYMOUS & vmm_flags) {
            kspinlock_acquire(&context->lock);
            virt = (void *)(context->anon_pages * ARCH_PAGE_SIZE +
                            CONFIG_VMM_ANON_START);
            context->anon_pages += size_in_pages;
            kspinlock_release(&context->lock);
        } else if (COM_MM_VMM_FLAGS_PHYSICAL & vmm_flags) {
            virt = (void *)ARCH_PHYS_TO_HHDM(page_paddr);
        } else {
            KASSERT(!"unsupported vmm flags");
        }
    } else {
        virt = (void *)((uintptr_t)virt & ~(ARCH_PAGE_SIZE - 1));
    }

    for (size_t i = 0; i < size_in_pages; i++) {
        uintptr_t offset = i * ARCH_PAGE_SIZE;
        void     *vaddr  = (void *)((uintptr_t)virt + offset);

        void *paddr;
        if (COM_MM_VMM_FLAGS_ALLOCATE & vmm_flags) {
            paddr = com_mm_pmm_alloc_zero();
        } else {
            paddr = (void *)((uintptr_t)page_paddr + offset);
        }

        bool success = arch_mmu_map(context->pagetable,
                                    vaddr,
                                    paddr,
                                    mmu_flags);
        KASSERT(success);
    }

    return virt;
}

void com_mm_vmm_switch(com_vmm_context_t *context) {
    context = vmm_ensure_context(context);
    arch_mmu_switch(context->pagetable);
}

void *com_mm_vmm_get_physical(com_vmm_context_t *context, void *virt_addr) {
    context = vmm_ensure_context(context);
    return arch_mmu_get_physical(context->pagetable, virt_addr);
}

void com_mm_vmm_init(void) {
    KLOG("initializing vmm");
    RootContext.pagetable = arch_mmu_get_table();
    RootContext.lock      = KSPINLOCK_NEW();
}
