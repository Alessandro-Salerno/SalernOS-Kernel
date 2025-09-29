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

#pragma once

#include <arch/info.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>

void com_mm_vmm_init(void) {
    arch_mmu_init();
}

com_vmm_context_t *com_mm_vmm_new_context(arch_mmu_pagetable_t *pagetable) {
    com_vmm_context_t *context = com_mm_slab_alloc(sizeof(com_vmm_context_t));
    context->pagetable         = pagetable;
    return context;
}

void com_mm_vmm_destroy_context(com_vmm_context_t *context) {
    arch_mmu_destroy_table(context->pagetable);
    com_mm_slab_free(context, sizeof(com_vmm_context_t));
}

com_vmm_context_t *com_mm_vmm_duplicate_context(com_vmm_context_t *context) {
    arch_mmu_pagetable_t *new_pt = arch_mmu_duplicate_table(context->pagetable);
    return com_mm_vmm_new_context(new_pt);
}

bool com_mm_vmm_map(com_vmm_context_t *context,
                    void              *virt,
                    void              *phys,
                    size_t             len,
                    int                vmm_flags,
                    arch_mmu_flags_t   mmu_flags) {
    void     *page_paddr = (void *)((uintptr_t)phys & ~(ARCH_PAGE_SIZE - 1));
    uintptr_t page_off   = (uintptr_t)phys % ARCH_PAGE_SIZE;
    size_t    size_p_off = page_off + len;
    size_t size_in_pages = (size_p_off + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;

    if (NULL == virt) {
        virt = (void *)ARCH_PHYS_TO_HHDM(page_paddr);
    } else {
        virt = (void *)((uintptr_t)virt % ARCH_PAGE_SIZE);
    }

    for (size_t i = 0; i < size_in_pages; i++) {
        uintptr_t offset = i * ARCH_PAGE_SIZE;
        void     *vaddr  = (void *)((uintptr_t)virt + offset);
        void     *paddr  = (void *)((uintptr_t)page_paddr + offset);
        arch_mmu_map(context->pagetable, vaddr, paddr, mmu_flags);
    }

    return (void *)ARCH_PHYS_TO_HHDM(addr);
}

void com_mm_vmm_switch(com_vmm_context_t *context) {
    arch_mmu_switch(context->pagetable);
}

void *com_mm_vmm_get_physical(com_vmm_context_t *context, void *virt_addr) {
    return arch_mmu_get_physical(context->pagetable, virt_addr);
}
