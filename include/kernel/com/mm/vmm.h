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

#include <arch/context.h>
#include <arch/mmu.h>
#include <lib/spinlock.h>
#include <stddef.h>
#include <vendor/tailq.h>

#define COM_MM_VMM_FLAGS_NONE      0
#define COM_MM_VMM_FLAGS_ANONYMOUS 1
#define COM_MM_VMM_FLAGS_NOHINT    2
#define COM_MM_VMM_FLAGS_PHYSICAL  4
#define COM_MM_VMM_FLAGS_FILE      8
#define COM_MM_VMM_FLAGS_EXACT     16
#define COM_MM_VMM_FLAGS_SHARED    32
#define COM_MM_VMM_FLAGS_REPLACE   64
#define COM_MM_VMM_FLAGS_ALLOCATE  128

#define COM_MM_VMM_FAULT_ATTR_COW        1 // COW enabled on the address
#define COM_MM_VMM_FAULT_ATTR_COW_NOEXEC 2 // Whether to set NOEXEC in COW

typedef struct com_vmm_context {
    arch_mmu_pagetable_t *pagetable;
    size_t                anon_pages;
    kspinlock_t           lock;
    TAILQ_ENTRY(com_vmm_context) reaper_entry;
} com_vmm_context_t;

void               com_mm_vmm_init(void);
void               com_mm_vmm_init_reaper(void);
com_vmm_context_t *com_mm_vmm_new_context(arch_mmu_pagetable_t *pagetable);
void               com_mm_vmm_destroy_context(com_vmm_context_t *context);
com_vmm_context_t *com_mm_vmm_duplicate_context(com_vmm_context_t *context);
void              *com_mm_vmm_map(com_vmm_context_t *context,
                                  void              *virt,
                                  void              *phys,
                                  size_t             len,
                                  int                vmm_flags,
                                  arch_mmu_flags_t   mmu_flags);
void *com_mm_vmm_get_physical(com_vmm_context_t *context, void *virt_addr);
void  com_mm_vmm_switch(com_vmm_context_t *context);
void  com_mm_vmm_handle_fault(void            *fault_virt,
                              void            *fault_phys,
                              arch_context_t  *fault_ctx,
                              arch_mmu_flags_t mmu_flags_hint,
                              int              attr);
