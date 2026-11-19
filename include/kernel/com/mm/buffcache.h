#pragma once

#include <kernel/com/sync/sync.h>
#include <kernel/com/util.h>

// second-cahcne hint
#define BUFFCACHE_REFERENCED (1 << 0)
// should be written back to storage
#define BUFFCACHE_DIRTY (1 << 1)
// page state is stable, retry from safe point
#define BUFFCACHE_BUSY (1 << 2)
// page eviction failed once, don't reattempt it
#define BUFFCACHE_ERROR (1 << 3)
// page is no-longer referneced by its device
#define BUFFCACHE_ORPHAN (1 << 4)

// write the page back without removing it from the device's lookup structure
#define BUFFCACHE_EVICT_SYNC (1 << 0)

typedef struct BufferPage BufferPage;

typedef int BufferEvictFn(void *device, u32 index, void *phys, int page_flags,
                          int evict_flags);

int buffcache_init(void);
BufferPage *buffcache_alloc(void);
void buffcache_own(BufferPage *page, void *device, u32 index,
                   BufferEvictFn *evict);
void buffcache_discard(BufferPage *page);
bool buffcache_hold(BufferPage *page, Sync *sync);
void buffcache_rel(BufferPage *page);
bool buffcache_free(BufferPage *page, Sync *sync);
void buffcache_mkdirty(BufferPage *page);
int buffcache_sync(void);
void *buffcache_get_phys(BufferPage *page);
