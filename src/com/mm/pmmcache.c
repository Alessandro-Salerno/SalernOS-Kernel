#include <arch/info.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/pmmcache.h>
#include <kernel/com/util.h>

#define HAS_AUTOLOCK(pmm_cache) \
    ((pmm_cache)->private.flags & PMM_CACHE_FLAGS_AUTOLOCK)
#define HAS_AUTOMERGE(pmm_cache) \
    ((pmm_cache)->private.flags & PMM_CACHE_FLAGS_AUTOMERGE)
#define HAS_AUTOALLOC(pmm_cache) \
    ((pmm_cache)->private.flags & PMM_CACHE_FLAGS_AUTOALLOC)

// NOTE: For those reading the code and wondering what this is for, it's
// basically just batching calls to pmm to improve performance in multi Cpu
// contexts (mostly)

struct Pool {
    struct Pool *next;
    usize len;
};

static void *pmm_cache_new_pool(usize pages) {
    struct Pool *pool = NULL;
    struct Pool *prev = NULL;

    for (usize allocated = 0; allocated < pages;) {
        usize curr_size;
        struct Pool *curr = (void *)PHYS_TO_HHDM(
            pmm_alloczn_max(pages, &curr_size));
        curr->next = NULL;
        curr->len = curr_size;

        if (prev == NULL) {
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

void *pmm_cache_alloc(PMMCache *pmm_cache, usize pages) {
    assert(pages == 1);

    if (HAS_AUTOLOCK(pmm_cache)) spinlock_acq(&pmm_cache->lock);

    void *ret = NULL;

    if (pmm_cache->private.pool == NULL) {
        assert(pmm_cache->private.avail_pages == 0);

        if (!HAS_AUTOALLOC(pmm_cache)) goto end;

        pmm_cache->private.pool = pmm_cache_new_pool(
            pmm_cache->private.pool_size);
        pmm_cache->private.avail_pages = pmm_cache->private.pool_size;
    }

    struct Pool *pool = pmm_cache->private.pool;
    ret = (void *)HHDM_TO_PHYS(pool);
    pmm_cache->private.avail_pages--;
    pool->len--;

    // If we ran out of space in the head poll
    if (pool->len == 0) {
        if (pool->next != NULL) {
            pmm_cache->private.pool = pool->next;
        } else {
            // Next call will either happen after a free or have to allocate its
            // own stuff
            pmm_cache->private.pool = NULL;
        }
    } else {
        // If this pool still has space left, we need to move the pointer
        // forwards
        uptr head_addr = (uptr)pool;
        uptr next_addr = head_addr + PAGE_SIZE;
        struct Pool *next_pool = (void *)next_addr;
        next_pool->len = pool->len;
        next_pool->next = pool->next;
        pmm_cache->private.pool = next_pool;
        // Length was decremented before
    }

end:
    if (HAS_AUTOLOCK(pmm_cache)) spinlock_rel(&pmm_cache->lock);

    if (ret != NULL) *pool = (struct Pool){0};

    return ret;
}

void pmm_cache_free(PMMCache *pmm_cache, void *page) {
    // We do this before acquiring the lock!
    // It is faster because preemption is enabled and it is safe because we
    // assume that merge is read-only
    struct Pool *virt_page = (void *)PHYS_TO_HHDM(page);
    if (!HAS_AUTOMERGE(pmm_cache)) memset(virt_page, 0, PAGE_SIZE);

    if (HAS_AUTOLOCK(pmm_cache)) spinlock_acq(&pmm_cache->lock);

    if (HAS_AUTOMERGE(pmm_cache)) {
        // TODO: try to merge into near entries
    } else {
        virt_page->next = pmm_cache->private.pool;
        virt_page->len = 1;
        pmm_cache->private.pool = virt_page;
        pmm_cache->private.avail_pages++;
    }

    if (HAS_AUTOLOCK(pmm_cache)) spinlock_rel(&pmm_cache->lock);
}

int pmm_cache_init(PMMCache *pmm_cache, usize init_size, usize pool_size,
                   usize max_pool_size, int flags) {
    pmm_cache->lock = SPINLOCK_NEW();
    pmm_cache->private.pool_size = pool_size;
    pmm_cache->private.max_pool_size = max_pool_size;
    pmm_cache->private.pool = pmm_cache_new_pool(init_size);
    pmm_cache->private.avail_pages = init_size;
    pmm_cache->private.flags = flags;
    return 0;
}
