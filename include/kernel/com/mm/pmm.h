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
#include <stddef.h>
#include <stdint.h>

#define COM_MM_PMM_ALLOC_BYTES(bytes) \
    com_mm_pmm_alloc_many_zero((bytes + (ARCH_PAGE_SIZE - 1)) / ARCH_PAGE_SIZE)
#define COM_MM_PMM_FREE_BYTES(ptr, bytes) \
    com_mm_pmm_free_many(ptr, (bytes + (ARCH_PAGE_SIZE - 1)) / ARCH_PAGE_SIZE)

typedef struct com_pmm_stats {
    size_t total;
    size_t usable;
    size_t reserved;
    size_t free;
    size_t used;
    size_t evictable;
    size_t to_zero;
    size_t to_insert;
    size_t to_defrag;
} com_pmm_stats_t;

void *com_mm_pmm_alloc(void);
void *com_mm_pmm_alloc_many(size_t pages);
void *com_mm_pmm_alloc_zero(void);
void *com_mm_pmm_alloc_many_zero(size_t pages);
void *com_mm_pmm_alloc_max(size_t *out_alloc_size, size_t pages);
void *com_mm_pmm_alloc_max_zero(size_t *out_alloc_size, size_t pages);
void  com_mm_pmm_hold(void *page);
void  com_mm_pmm_free(void *page);
void  com_mm_pmm_free_many(void *base, size_t pages);
void  com_mm_pmm_get_stats(com_pmm_stats_t *out);
void  com_mm_pmm_init_threads(void);
void  com_mm_pmm_init(void);
