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

#include <arch/info.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/pmmcache.h>
#include <lib/mem.h>
#include <lib/util.h>

// NOTE: For those reading the code and wondering what this is for, it's
// basically just batching calls to pmm to improve performance in multi CPU
// contexts (mostly)

struct pmm_cache_pool {
    struct pmm_cache_pool *next;
    size_t                 len;
};

static void *pmm_cache_new_pool(size_t pages) {
    struct pmm_cache_pool *pool = (void *)ARCH_PHYS_TO_HHDM(
        com_mm_pmm_alloc_zero());
    struct pmm_cache_pool *curr = pool;

    for (size_t i = 0; i < pages - 1; i++) {
        struct pmm_cache_pool *next = (void *)ARCH_PHYS_TO_HHDM(
            com_mm_pmm_alloc_zero());
        curr->next = next;
        curr->len  = 1;
        curr       = next;
    }

    curr->next = NULL;
    curr->len  = 1;
    return pool;
}

void *com_mm_pmm_cache_alloc(com_pmm_cache_t *pmm_cache, size_t pages) {
    KASSERT(1 == pages);

    if (pmm_cache->autolock) {
        kspinlock_acquire(&pmm_cache->lock);
    }

    if (NULL == pmm_cache->pool) {
        KASSERT(0 == pmm_cache->private.avail_pages);
        pmm_cache->pool = pmm_cache_new_pool(pmm_cache->pool_size);
        pmm_cache->private.avail_pages = pmm_cache->pool_size;
    }

    struct pmm_cache_pool *pool = pmm_cache->pool;
    void                  *ret  = (void *)ARCH_HHDM_TO_PHYS(pool);
    pmm_cache->private.avail_pages--;
    pool->len--;

    // If we ran out of space in the head poll
    if (0 == pool->len) {
        if (NULL != pool->next) {
            pmm_cache->pool = pool->next;
        } else {
            // Next call will either happen after a free or have to allocate its
            // own stuff
            pmm_cache->pool = NULL;
        }
    } else {
        // If this pool still has space left, we need to move the pointer
        // forwards
        uintptr_t              head_addr = (uintptr_t)pool;
        uintptr_t              next_addr = head_addr + ARCH_PAGE_SIZE;
        struct pmm_cache_pool *next_pool = (void *)next_addr;
        next_pool->len                   = pool->len;
        next_pool->next                  = pool->next;
        pmm_cache->pool                  = next_pool;
        // Length was decremented before
    }

    if (pmm_cache->autolock) {
        kspinlock_release(&pmm_cache->lock);
    }

    *pool = (struct pmm_cache_pool){0};
    return ret;
}

void com_mm_pmm_cache_free(com_pmm_cache_t *pmm_cache, void *page) {
    // We do this before acquiring the lock!
    // It is faster because preemption is enabled and it is safe because we
    // assume that merge is read-only
    struct pmm_cache_pool *virt_page = (void *)ARCH_PHYS_TO_HHDM(page);
    if (!pmm_cache->merge) {
        kmemset(virt_page, ARCH_PAGE_SIZE, 0);
    }

    if (pmm_cache->autolock) {
        kspinlock_acquire(&pmm_cache->lock);
    }

    if (pmm_cache->private.avail_pages > pmm_cache->max_pool_size) {
        com_mm_pmm_free(page);
    } else if (pmm_cache->merge) {
        // TODO: try to merge into near entries
    } else {
        virt_page->next = pmm_cache->pool;
        virt_page->len  = 1;
        pmm_cache->pool = virt_page;
        pmm_cache->private.avail_pages++;
    }

    if (pmm_cache->autolock) {
        kspinlock_release(&pmm_cache->lock);
    }
}

int com_mm_pmm_cache_init(com_pmm_cache_t *pmm_cache,
                          size_t           init_size,
                          size_t           pool_size,
                          size_t           max_pool_size,
                          bool             autolock,
                          bool             merge) {
    pmm_cache->lock                = KSPINLOCK_NEW();
    pmm_cache->autolock            = autolock;
    pmm_cache->pool_size           = pool_size;
    pmm_cache->max_pool_size       = max_pool_size;
    pmm_cache->merge               = merge;
    pmm_cache->pool                = pmm_cache_new_pool(init_size);
    pmm_cache->private.avail_pages = init_size;
    return 0;
}
