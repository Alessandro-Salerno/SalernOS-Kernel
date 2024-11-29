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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ARCH_MMU_KERNELSPACE_START (void *)0xffff800000000000
#define ARCH_MMU_KERNELSPACE_END   (void *)0xffffffffffffffff
#define ARCH_MMU_USERSPACE_START   (void *)0x0000000000001000
#define ARCH_MMU_USERSPACE_END     (void *)0x0000800000000000

#define ARCH_MMU_ADDR_IS_USER(a) ((void *)a < USERSPACE_END)

#define ARCH_MMU_FLAGS_READ   (uint64_t)1
#define ARCH_MMU_FLAGS_WRITE  (uint64_t)2
#define ARCH_MMU_FLAGS_USER   (uint64_t)4
#define ARCH_MMU_FLAGS_NOEXEC ((uint64_t)1 << 63)

#define ARCH_MMU_FLAGS_WB 0
#define ARCH_MMU_FLAGS_WT (1 << 3)
#define ARCH_MMU_FLAGS_UC (1 << 4)

typedef uint64_t arch_mmu_pagetable_t;
typedef uint64_t arch_mmu_flags_t;

void                  arch_mmu_init(void);
arch_mmu_pagetable_t *arch_mmu_new_tabe(void);
void                  arch_mmu_destroy_table(arch_mmu_pagetable_t *pt);
arch_mmu_pagetable_t  arch_mmu_duplicate_table(arch_mmu_pagetable_t *pt);
bool                  arch_mmu_map(arch_mmu_pagetable_t *pt,
                                   void                 *virt,
                                   void                 *phys,
                                   arch_mmu_flags_t      flags);
void                  arch_mmu_switch(arch_mmu_pagetable_t *pt);
bool                  arch_mmu_ispresent(arch_mmu_pagetable_t *pt, void *virt);
bool                  arch_mmu_iswritable(arch_mmu_pagetable_t *pt, void *virt);
bool                  arch_mmu_isdirty(arch_mmu_pagetable_t *pt, void *virt);
