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

#include <arch/info.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/pmmcache.h>
#include <lib/mem.h>
#include <lib/util.h>

#define HAS_AUTOLOCK(pmm_cache) \
    (COM_MM_PMM_CACHE_FLAGS_AUTOLOCK & (pmm_cache)->private.flags)
#define HAS_AUTOMERGE(pmm_cache) \
    (COM_MM_PMM_CACHE_FLAGS_AUTOMERGE & (pmm_cache)->private.flags)
#define HAS_AUTOALLOC(pmm_cache) \
    (COM_MM_PMM_CACHE_FLAGS_AUTOALLOC & (pmm_cache)->private.flags)

// NOTE: For those reading the code and wondering what this is for, it's
// basically just batching calls to pmm to improve performance in multi CPU
// contexts (mostly)

struct pool {
    struct pool *next;
    size_t       len;
};

static void *pmm_cache_new_pool(size_t pages) {
    struct pool *pool = NULL;
    struct pool *prev = NULL;

    for (size_t allocated = 0; allocated < pages;) {
        size_t       curr_size;
        struct pool *curr = (void *)ARCH_PHYS_TO_HHDM(
            com_mm_pmm_alloc_max_zero(&curr_size, pages));
        curr->next = NULL;
        curr->len  = curr_size;

        if (NULL == prev) {
            pool = curr;
            prev = curr;
        } else {
            prev->next = curr;
        }

        prev = curr;
        allocated += curr_size;
    }

    return pool;
}

void *com_mm_pmm_cache_alloc(com_pmm_cache_t *pmm_cache, size_t pages) {
    KASSERT(1 == pages);

    if (HAS_AUTOLOCK(pmm_cache)) {
        kspinlock_acquire(&pmm_cache->lock);
    }

    void *ret = NULL;

    if (NULL == pmm_cache->private.pool) {
        KASSERT(0 == pmm_cache->private.avail_pages);

        if (!HAS_AUTOALLOC(pmm_cache)) {
            goto end;
        }

        pmm_cache->private.pool = pmm_cache_new_pool(
            pmm_cache->private.pool_size);
        pmm_cache->private.avail_pages = pmm_cache->private.pool_size;
    }

    struct pool *pool = pmm_cache->private.pool;
    ret               = (void *)ARCH_HHDM_TO_PHYS(pool);
    pmm_cache->private.avail_pages--;
    pool->len--;

    // If we ran out of space in the head poll
    if (0 == pool->len) {
        if (NULL != pool->next) {
            pmm_cache->private.pool = pool->next;
        } else {
            // Next call will either happen after a free or have to allocate its
            // own stuff
            pmm_cache->private.pool = NULL;
        }
    } else {
        // If this pool still has space left, we need to move the pointer
        // forwards
        uintptr_t    head_addr  = (uintptr_t)pool;
        uintptr_t    next_addr  = head_addr + ARCH_PAGE_SIZE;
        struct pool *next_pool  = (void *)next_addr;
        next_pool->len          = pool->len;
        next_pool->next         = pool->next;
        pmm_cache->private.pool = next_pool;
        // Length was decremented before
    }

end:
    if (HAS_AUTOLOCK(pmm_cache)) {
        kspinlock_release(&pmm_cache->lock);
    }

    if (NULL != ret) {
        *pool = (struct pool){0};
    }

    return ret;
}

void com_mm_pmm_cache_free(com_pmm_cache_t *pmm_cache, void *page) {
    // We do this before acquiring the lock!
    // It is faster because preemption is enabled and it is safe because we
    // assume that merge is read-only
    struct pool *virt_page = (void *)ARCH_PHYS_TO_HHDM(page);
    if (!HAS_AUTOMERGE(pmm_cache)) {
        kmemset(virt_page, ARCH_PAGE_SIZE, 0);
    }

    if (HAS_AUTOLOCK(pmm_cache)) {
        kspinlock_acquire(&pmm_cache->lock);
    }

    if (HAS_AUTOMERGE(pmm_cache)) {
        // TODO: try to merge into near entries
    } else {
        virt_page->next         = pmm_cache->private.pool;
        virt_page->len          = 1;
        pmm_cache->private.pool = virt_page;
        pmm_cache->private.avail_pages++;
    }

    if (HAS_AUTOLOCK(pmm_cache)) {
        kspinlock_release(&pmm_cache->lock);
    }
}

int com_mm_pmm_cache_init(com_pmm_cache_t *pmm_cache,
                          size_t           init_size,
                          size_t           pool_size,
                          size_t           max_pool_size,
                          int              flags) {
    pmm_cache->lock                  = KSPINLOCK_NEW();
    pmm_cache->private.pool_size     = pool_size;
    pmm_cache->private.max_pool_size = max_pool_size;
    pmm_cache->private.pool          = pmm_cache_new_pool(init_size);
    pmm_cache->private.avail_pages   = init_size;
    pmm_cache->private.flags         = flags;
    return 0;
}
