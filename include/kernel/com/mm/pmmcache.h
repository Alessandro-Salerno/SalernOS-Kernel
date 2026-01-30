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

#include <lib/spinlock.h>
#include <stddef.h>
#include <stdint.h>

#define COM_MM_PMM_CACHE_FLAGS_AUTOLOCK  1
#define COM_MM_PMM_CACHE_FLAGS_AUTOMERGE 2
#define COM_MM_PMM_CACHE_FLAGS_AUTOALLOC 4

typedef struct com_pmm_cache {
    kspinlock_t lock;
    struct {
        size_t pool_size;
        size_t max_pool_size;
        void  *pool;
        size_t avail_pages;
        int    flags;
    } private;
} com_pmm_cache_t;

int   com_mm_pmm_cache_init(com_pmm_cache_t *pmm_cache,
                            size_t           init_size,
                            size_t           pool_size,
                            size_t           max_pool_size,
                            int              flags);
void *com_mm_pmm_cache_alloc(com_pmm_cache_t *pmm_cache, size_t pages);
void  com_mm_pmm_cache_free(com_pmm_cache_t *pmm_cache, void *page);
