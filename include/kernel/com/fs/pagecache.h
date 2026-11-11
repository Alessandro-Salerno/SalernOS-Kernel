#pragma once

#include <arch/info.h>
#include <arch/mmu.h>
#include <kernel/com/ds/radixtree.h>
#include <kernel/com/util.h>

typedef RadixTree PageCache;

#define PAGECACHE_LAYERS 4

#define PAGECACHE_MAX_PAGES \
    ((u64)1 << (RADIXTREE_INDEX_BITS * PAGECACHE_LAYERS))

bool pagecache_get(PageCache *root, u64 index, uptr *out_addr);
void pagecache_default(PageCache *root, u64 index, uptr *out_addr);
PageCache *pagecache_new(void);
// Pages that are still mapped stay alive through their mappings
void pagecache_free(PageCache *root);
