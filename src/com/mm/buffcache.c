#include <errno.h>
#include <kernel/com/mm/buffcache.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>

// WARNING: this buffer cache was implemented just to rush features, it is not
// good in any way

typedef struct BufferPage {
    BufferEvictFn *evict;
    void *device;
    u32 index;
    int flags;
} BufferPage;

static Spinlock g_buffer_lock = SPINLOCK_NEW();
static Waitlist g_buffer_waitlist;
static u32 g_buffer_size = 0;
static void *g_buffer_cache = NULL;
static BufferPage *g_page_db = NULL;
static u32 g_clock_hand = 0;
static u32 g_in_flight = 0;

static u32 page_to_index(BufferPage *page) { return page - g_page_db; }
static void *page_to_phys(BufferPage *page) {
    return (void *)HHDM_TO_PHYS((u8 *)g_buffer_cache +
                                (page_to_index(page) * PAGE_SIZE));
}

static bool page_is_owned(BufferPage *page) {
    return !(page->flags & BUFFCACHE_ORPHAN);
}
static bool page_is_shared(BufferPage *page) {
    // The entire buffer cache is allocated as a contiguous physical block of
    // PAGE_STATE_ALLOCATED pages. This means that each one has a real reference
    // count kept by the PMM:
    //      - 1 reference = the buffer cache is the only one referencing (i.e.,
    //      free)
    //      - 2 references && !(flags & BUFFCACHE_ORPHAN) = the page is
    //      referenced only by its owner device (i.e., it can be evicted)
    //      - >= 3 references = page is shared and cannot be evicted
    // The BUFFCACHE_ORPHAN flag is necessary because otherwise there would be
    // no way to distinguish the "base reference" of the owner device from a
    // transient reference by code concurrently running operations on it. When
    // BUFFCACHE_ORPHAN is set, the base reference is assumed to have already
    // been dropped
    void *phys = page_to_phys(page);
    usize ref = pmm_get_num_ref(phys);
    return ref > (page_is_owned(page) ? 2 : 1);
}

// GENERIC HELPERS

static void buffcache_lock(void) { spinlock_acq(&g_buffer_lock); }
static void buffcache_unlock(void) { spinlock_rel(&g_buffer_lock); }
static bool buffcache_is_locked(void) {
    return spinlock_is_held(&g_buffer_lock);
}
static bool buffcache_is_initialized(void) { return g_buffer_cache != NULL; }

static int buffcache_evict(BufferPage *page, int evict_flags) {
    assert(buffcache_is_locked());
    assert(buffcache_is_initialized());
    assert(page != NULL);
    assert(page >= g_page_db && page < g_page_db + g_buffer_size);
    assert(!(page->flags & BUFFCACHE_BUSY));
    assert(!(page->flags & BUFFCACHE_ERROR));
    assert(!page_is_shared(page));

    page->flags |= BUFFCACHE_BUSY;
    g_in_flight++;
    buffcache_unlock();

    int evict_ret = 0;
    if (page->evict != NULL && page_is_owned(page))
        evict_ret = page->evict(page->device, page->index, page_to_phys(page),
                                page->flags, evict_flags);
    if (evict_ret != 0) {
        buffcache_lock();
        page->flags &= ~BUFFCACHE_BUSY;
        page->flags |= BUFFCACHE_ERROR;
        g_in_flight--;
        sched_notify_all(&g_buffer_waitlist);
        buffcache_unlock();
        return evict_ret;
    }

    buffcache_lock();
    page->evict = NULL;
    page->device = NULL;
    page->index = 0;
    page->flags = 0;
    g_in_flight--;
    sched_notify_all(&g_buffer_waitlist);
    // We don't unlock here, because unlocking would expose a transition state
    // we don't want to handle right now. The caller is responsible for setting
    // its own fields and releasing the lock
    return 0;
}

BufferPage *buffcache_alloc(void) {
    buffcache_lock();

    // TODO: this is really slow, it was built just for correctness and not for
    // speed. Aside from the global lock and the variable-modulo below, it of
    // course performs an unnecessary full scan. The new design (out of tree)
    // properly uses dirty/clean recency lists etc.

    // We scan pages twice because the first time we may have just dropped the
    // referenced bit without actually grabbing any page
    for (u64 i = 0; i < 2 * (u64)g_buffer_size; i++) {
        BufferPage *page = &g_page_db[g_clock_hand];
        g_clock_hand = (g_clock_hand + 1) % g_buffer_size;

        if (page->flags & (BUFFCACHE_BUSY | BUFFCACHE_ERROR) ||
            page_is_shared(page)) {
            continue;
        }
        if (page->flags & BUFFCACHE_REFERENCED) {
            page->flags &= ~BUFFCACHE_REFERENCED;
            continue;
        }
        if (buffcache_evict(page, 0) != 0) {
            buffcache_lock();
            continue;
        }

        page->flags = BUFFCACHE_BUSY | BUFFCACHE_ORPHAN;
        // An evicted page still has its owner's reference, which becomes ours
        void *phys = page_to_phys(page);
        if (pmm_get_num_ref(phys) == 1) pmm_hold(phys);
        buffcache_unlock();
        return page;
    }

    // TODO: use the in-flight counter to handle edge-cases better

    buffcache_unlock();
    return NULL;
}

void buffcache_own(BufferPage *page, void *device, u32 index,
                   BufferEvictFn *evict) {
    buffcache_lock();
    assert(page->flags & BUFFCACHE_BUSY);
    assert(!page_is_owned(page));
    page->evict = evict;
    page->device = device;
    page->index = index;
    page->flags &= ~(BUFFCACHE_BUSY | BUFFCACHE_ORPHAN);
    sched_notify_all(&g_buffer_waitlist);
    buffcache_unlock();
}

void buffcache_discard(BufferPage *page) {
    buffcache_lock();
    assert(page->flags & BUFFCACHE_BUSY);
    assert(!page_is_owned(page));
    page->flags &= ~BUFFCACHE_BUSY;
    pmm_free(page_to_phys(page));
    sched_notify_all(&g_buffer_waitlist);
    buffcache_unlock();
}

bool buffcache_hold(BufferPage *page, Sync *sync) {
    buffcache_lock();

    if (page->flags & BUFFCACHE_BUSY) {
        // Whoever made the page busy may need this same sync to finish
        sync_rel(sync);
        spinlock_wait(&g_buffer_lock, &g_buffer_waitlist, 0, false);
        buffcache_unlock();
        sync_acq(sync);
        return false;
    }

    assert(page_is_owned(page));
    page->flags |= BUFFCACHE_REFERENCED;
    pmm_hold(page_to_phys(page));

    buffcache_unlock();
    return true;
}

void buffcache_rel(BufferPage *page) {
    // We do this under the global lock as well because even if the PMM has its
    // own synchronization on page references, we want our decisions made in
    // other functions not to become stable
    buffcache_lock();
    void *phys = page_to_phys(page);
    assert(pmm_get_num_ref(phys) > 1);
    pmm_free(phys);
    buffcache_unlock();
}

bool buffcache_free(BufferPage *page, Sync *sync) {
    buffcache_lock();

    if (page->flags & BUFFCACHE_BUSY) {
        // Writeback may be in flight, its data must not land on freed blocks
        sync_rel(sync);
        spinlock_wait(&g_buffer_lock, &g_buffer_waitlist, 0, false);
        buffcache_unlock();
        sync_acq(sync);
        return false;
    }

    assert(page_is_owned(page));
    page->flags |= BUFFCACHE_ORPHAN;
    pmm_free(page_to_phys(page));

    buffcache_unlock();
    return true;
}

void buffcache_mkdirty(BufferPage *page) {
    buffcache_lock();
    assert(page_is_shared(page));
    page->flags |= BUFFCACHE_DIRTY;
    buffcache_unlock();
}

// As an over-approximation, this can be used to implement any sync API:
// sync'ing the whole buffer cahce is >= sync'ing a file range. Inefficient but
// correct
int buffcache_sync(void) {
    int ret = 0;
    buffcache_lock();

    for (u32 i = 0; i < g_buffer_size; i++) {
        BufferPage *page = &g_page_db[i];
        if (!page_is_owned(page) || page->evict == NULL ||
            page->flags & BUFFCACHE_BUSY || !(page->flags & BUFFCACHE_DIRTY)) {
            continue;
        }

        // Clear dirty before writing, so that concurrent writers re-set it
        int page_flags = page->flags | BUFFCACHE_BUSY;
        page->flags = page_flags & ~BUFFCACHE_DIRTY;
        g_in_flight++;
        buffcache_unlock();

        int sync_ret = page->evict(page->device, page->index,
                                   page_to_phys(page), page_flags,
                                   BUFFCACHE_EVICT_SYNC);

        buffcache_lock();
        page->flags &= ~BUFFCACHE_BUSY;
        if (sync_ret == 0) page->flags &= ~BUFFCACHE_ERROR;
        else {
            page->flags |= BUFFCACHE_DIRTY;
            ret = sync_ret;
        }
        g_in_flight--;
        sched_notify_all(&g_buffer_waitlist);
    }

    buffcache_unlock();
    return ret;
}

void *buffcache_get_phys(BufferPage *page) { return page_to_phys(page); }

int buffcache_init(void) {
    log("initializing buffer cache");

    PMMStats stats = {0};
    pmm_get_stats(&stats);

    usize free_pages = stats.free / PAGE_SIZE;
    usize buff_pages = min(free_pages / 10000 * CONFIG_BUFFCACHE_SIZE,
                           UINT32_MAX);
    log_debug("%zu free pages, taking ~%zu%% of it = %zu pages", free_pages,
              CONFIG_BUFFCACHE_SIZE / 100, buff_pages);
    void *buff_phys = pmm_allocn(buff_pages);
    if (buff_phys == NULL) return ENOMEM;

    void *buff_descs_phys = pmm_allocn(
        buff_pages * sizeof(BufferPage) / PAGE_SIZE + 1);
    if (buff_descs_phys == NULL) {
        pmm_freen(buff_phys, buff_pages);
        return ENOMEM;
    }

    buffcache_lock();
    waitlist_init(&g_buffer_waitlist);
    g_buffer_size = buff_pages;
    g_buffer_cache = (void *)PHYS_TO_HHDM(buff_phys);
    g_page_db = (void *)PHYS_TO_HHDM(buff_descs_phys);
    buffcache_unlock();

    return 0;
}
