#pragma once

#include <kernel/com/sync/spinlock.h>
#include <kernel/com/util.h>

#define PMM_CACHE_FLAGS_AUTOLOCK  1
#define PMM_CACHE_FLAGS_AUTOMERGE 2
#define PMM_CACHE_FLAGS_AUTOALLOC 4

typedef struct PMMCache {
    Spinlock lock;
    struct {
        usize pool_size;
        usize max_pool_size;
        void *pool;
        usize avail_pages;
        int flags;
    } private;
} PMMCache;

int pmm_cache_init(PMMCache *pmm_cache, usize init_size, usize pool_size,
                   usize max_pool_size, int flags);
void *pmm_cache_alloc(PMMCache *pmm_cache, usize pages);
void pmm_cache_free(PMMCache *pmm_cache, void *page);
