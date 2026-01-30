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

#pragma once

#include <arch/mmu.h>
#include <stddef.h>

void                  arch_mmu_init(void);
arch_mmu_pagetable_t *arch_mmu_new_table(void);
void                  arch_mmu_destroy_table(arch_mmu_pagetable_t *pt);
arch_mmu_pagetable_t *arch_mmu_duplicate_table(arch_mmu_pagetable_t *pt);
bool                  arch_mmu_map(arch_mmu_pagetable_t *pt,
                                   void                 *virt,
                                   void                 *phys,
                                   arch_mmu_flags_t      flags);
bool                  arch_mmu_chflags(arch_mmu_flags_t *pt,
                                       void             *virt,
                                       arch_mmu_flags_t  new_flags);
bool  arch_mmu_unmap(void **out_old_phys, arch_mmu_pagetable_t *pt, void *virt);
void  arch_mmu_invalidate(arch_mmu_pagetable_t *pt, void *virt, size_t pages);
void  arch_mmu_switch(arch_mmu_pagetable_t *pt);
void  arch_mmu_switch_default(void);
void *arch_mmu_get_physical(arch_mmu_pagetable_t *pagetable, void *virt_addr);
bool  arch_mmu_is_cow(arch_mmu_pagetable_t *pagetable, void *virt_addr);
bool  arch_mmu_is_executable(arch_mmu_pagetable_t *pagetable, void *virt_addr);
arch_mmu_pagetable_t *arch_mmu_get_table(void);
