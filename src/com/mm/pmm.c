#include <arch/info.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/ds/hashmap.h>
#include <kernel/com/ds/searchtree.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sync/sync.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/panic.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/platform/info.h>
#include <vendor/printf.h>
#include <vendor/tailq.h>

#define FREELIST_LOCK(freelist_head)   spinlock_acq(&(freelist_head)->lock);
#define FREELIST_UNLOCK(freelist_head) spinlock_rel(&(freelist_head)->lock);

#define PHYS_TO_META_INDEX(page_phys) ((uptr)(page_phys) / PAGE_SIZE)
#define PAGE_META_ARRAY_LEN(memsize)  ((memsize) / PAGE_SIZE)
#define PAGE_META_ARRAY_SIZE(memsize) \
    (PAGE_META_ARRAY_LEN(memsize) * sizeof(struct PageMeta))

#define UPDATE_STATS(field, diff) \
    __atomic_add_fetch(&g_memory_stats.field, diff *PAGE_SIZE, __ATOMIC_RELAXED)
#define READ_STATS(field) \
    __atomic_load_n(&g_memory_stats.field, __ATOMIC_RELAXED)

TAILQ_HEAD(FreelistTailQ, FreelistEntry);

struct FreelistHead {
    struct FreelistTailQ head;
    Spinlock lock;
};

// A block of one or more contigous pages that points to two more such blocks
// (one before, one after)
struct FreelistEntry {
    TAILQ_ENTRY(FreelistEntry) tqe;
    usize pages;
};

enum PageState {
    // NOTE: this must be zero, since all memory is zeroed on boot, and all page
    // structs in the array are assumed to report a free state
    PAGE_STATE_FREE = 0,
    PAGE_STATE_RESERVED,
    PAGE_STATE_TAKEN
};

enum PageType {
    PAGE_TYPE_ANONYMOUS,
    PAGE_TYPE_FILE
};

// An entry into the global page array that holds data about the page which is
// situated outside the page tiself. The index into the array is calculated as
// follows:
//      idx = page_phys_addr / PAGE_SIZE
struct PageMeta {
    enum PageState state;
    usize num_ref;

    enum PageType type;
    union {};
};

struct PageMetaArray {
    struct PageMeta *buffer;
    usize len;
    // Having a page meta array the size of physical memory is too expensive. We
    // thus have an array that goes up to the highest could-be-usable address.
    // Everything above that is an imaginary reserved entry
    usize reserved_start;
    usize extended_len;
    struct PageMeta fake_entry;
};

// GLOBAL VARIABLES

// List of currently available blocks. This list is ordered by block size
static struct FreelistHead g_main_free_list = {0};
// Array of page metadata. Initialized by the init() function to piont to a the
// HHDMInfo representation of a memory map entry large enough to hold it
static struct PageMetaArray g_page_meta = {0};
// Consolidated data for fast access. Should be accessed atomically after
// initialization
static PMMStats g_memory_stats = {0};

// UTILITY FUNCTIONS

// Freelists are ordered, so insertion should be too
static inline void freelist_add_ordered_nolock(struct FreelistHead *freelist,
                                               struct FreelistEntry *entry) {
    // Fast path for 1-page sized inserts. These will always go to the end
    if (entry->pages == 1) {
        struct FreelistEntry *curr_last = TAILQ_LAST(&freelist->head,
                                                     FreelistTailQ);
        // If the freelist is empty, we just add the entry
        if (curr_last == NULL) goto fallback;
        // If it comes right after the last entry, we merge the two
        if ((uptr)entry == (uptr)curr_last + curr_last->pages * PAGE_SIZE) {
            curr_last->pages++;
            return;
        }
        // If it comes right before, we do the same
        if ((uptr)curr_last == (uptr)entry + PAGE_SIZE) {
            entry->pages += curr_last->pages;
            TAILQ_REMOVE(&freelist->head, curr_last, tqe);
        }
        // If the two entries exist, but are not contiguous, or the new entry is
        // contiguous but comes before (last if statement), we ad the new entryu
        // to the end
        goto fallback;
    }

    struct FreelistEntry *curr, *_;
    TAILQ_FOREACH_SAFE(curr, &freelist->head, tqe, _) {
        if (entry->pages >= curr->pages) {
            TAILQ_INSERT_BEFORE(curr, entry, tqe);
            return;
        }
    }

fallback:
    // Either the freelist is empty or this entry is the smallest. Either way,
    // just add it to the end
    TAILQ_INSERT_TAIL(&freelist->head, entry, tqe);
}

// Freelists are not just ordered, they're faster if they contain fewer entries.
// Thus, before adding an entry, we should check if it is just a continuation of
// a preexisting entry. The reason why we keep the "basic oerdered" function is
// that this one is slower, so it should be called as rarely as possiblle,
// whereas the other one might be called by some allocation code.
// NOTE: This functions does not perform defragmentation
static inline void freelist_add_merged_nolock(struct FreelistHead *freelist,
                                              struct FreelistEntry *entry) {
    uptr entry_first_page = (uptr)entry;
    uptr entry_last_page = entry_first_page + (entry->pages - 1) * PAGE_SIZE;

    struct FreelistEntry *curr, *_;
    TAILQ_FOREACH_SAFE(curr, &freelist->head, tqe, _) {
        uptr curr_first_page = (uptr)curr;
        uptr curr_last_page = curr_first_page + (entry->pages - 1) * PAGE_SIZE;

        if (curr_first_page == entry_last_page + PAGE_SIZE) {
            entry->pages += curr->pages;
            TAILQ_INSERT_BEFORE(curr, entry, tqe);
            TAILQ_REMOVE(&freelist->head, curr, tqe);
            curr = entry;
            return;
        }

        if (curr_last_page == entry_first_page - PAGE_SIZE) {
            curr->pages += entry->pages;
            return;
        }
    }

    // Could not be merged with any existing entry, so we jsut add it as a
    // separate one
    freelist_add_ordered_nolock(freelist, entry);
}

// Since the freelist is ordered, taking from the rear means taking from the
// smaller chunks, which is good when allocating single pages
static inline struct FreelistEntry *
freelist_pop_rear_nolock(struct FreelistHead *freelist) {
    struct FreelistEntry *last = TAILQ_LAST(&g_main_free_list.head,
                                            FreelistTailQ);
    assert(last != NULL);

    if (last->pages == 1) {
        TAILQ_REMOVE(&freelist->head, last, tqe);
    } else {
        last->pages--;
        last = (void *)((uptr)last + PAGE_SIZE * last->pages);
    }

    return last;
}

// When allocating multiple pages, however, we take them from the front, because
// it is more likelly that we'll find a match immediatelly
static inline struct FreelistEntry *
freelist_pop_front_nolock(struct FreelistHead *freelist, usize num_pages,
                          usize *out_alloc_size) {
    struct FreelistEntry *ret = TAILQ_FIRST(&freelist->head);
    assert(ret != NULL);
    usize alloc_size = min(num_pages, ret->pages);

    struct FreelistEntry *entry = ret;
    struct FreelistEntry *next_entry = TAILQ_NEXT(entry, tqe);

    if (alloc_size == ret->pages) {
        TAILQ_REMOVE(&freelist->head, ret, tqe);
    } else {
        ret->pages -= alloc_size;
        ret = (void *)((uptr)ret + PAGE_SIZE * ret->pages);
    }

    // Since we decrement the number of pages in this entry, it may now longer
    // be the largest
    if (next_entry != NULL && next_entry->pages > entry->pages) {
        TAILQ_REMOVE(&freelist->head, entry, tqe);
        freelist_add_ordered_nolock(freelist, entry);
    }

    *out_alloc_size = alloc_size;
    return ret;
}

static inline struct PageMeta *page_meta_get(void *phys_addr) {
    usize index = PHYS_TO_META_INDEX(phys_addr);
    assert(index < g_page_meta.extended_len);

    if (index < g_page_meta.reserved_start) return &g_page_meta.buffer[index];

    return &g_page_meta.fake_entry;
}

static inline void page_meta_set(void *page_phys, struct PageMeta *tocopy,
                                 usize num_pages) {
    uptr end_addr = (uptr)page_phys + num_pages * PAGE_SIZE;
    usize index1 = PHYS_TO_META_INDEX(page_phys);
    usize index2 = PHYS_TO_META_INDEX(end_addr) - 1;
    assert(index1 < g_page_meta.len);
    assert(index2 < g_page_meta.len);

    memrepcpy(page_meta_get(page_phys), tocopy, sizeof(struct PageMeta),
              num_pages);
}

// INTERFACE FUNCTIONS

void *pmm_alloc(void) {
    ProfilerData profiler_data = profiler_start_function(
        PROFILE_FUNC_PMM_ALLOC);
    FREELIST_LOCK(&g_main_free_list);
    struct FreelistEntry *virt_ret = freelist_pop_rear_nolock(
        &g_main_free_list);
    FREELIST_UNLOCK(&g_main_free_list);

    *virt_ret = (struct FreelistEntry){0};
    void *phys = (void *)HHDM_TO_PHYS(virt_ret);
    struct PageMeta *page_meta = page_meta_get(phys);

    assert(page_meta->state == PAGE_STATE_FREE);
    assert(page_meta->num_ref == 0);
    page_meta->num_ref = 1;
    page_meta->state = PAGE_STATE_TAKEN;
    page_meta->type = PAGE_TYPE_ANONYMOUS;

    UPDATE_STATS(allocated, +1);
    UPDATE_STATS(free, -1);

    profiler_end_function(&profiler_data);
    return phys;
}

void *pmm_allocn(usize pages) {
    if (pages == 1) return pmm_alloc();

    ProfilerData profiler_data = profiler_start_function(
        PROFILE_FUNC_PMM_ALLOC);
    FREELIST_LOCK(&g_main_free_list);
    usize alloc_size;
    struct FreelistEntry *virt_ret = freelist_pop_front_nolock(
        &g_main_free_list, pages, &alloc_size);
    FREELIST_UNLOCK(&g_main_free_list);
    assert(alloc_size == pages);

    *virt_ret = (struct FreelistEntry){0};
    void *phys = (void *)HHDM_TO_PHYS(virt_ret);

    struct PageMeta model = {.num_ref = 1,
                             .state = PAGE_STATE_TAKEN,
                             .type = PAGE_TYPE_ANONYMOUS};
    page_meta_set(phys, &model, pages);

    UPDATE_STATS(allocated, +pages);
    UPDATE_STATS(free, -pages);

    profiler_end_function(&profiler_data);
    return phys;
}

void *pmm_allocz(void) { return pmm_alloc(); }

void *pmm_alloczn(usize pages) { return pmm_allocn(pages); }

void *pmm_allocn_max(usize pages, usize *out_alloc_size) {
    if (pages == 1) {
        if (out_alloc_size != NULL) *out_alloc_size = 1;
        return pmm_alloc();
    }

    FREELIST_LOCK(&g_main_free_list);
    usize alloc_size;
    struct FreelistEntry *virt_ret = freelist_pop_front_nolock(
        &g_main_free_list, pages, &alloc_size);
    FREELIST_UNLOCK(&g_main_free_list);

    *virt_ret = (struct FreelistEntry){0};
    void *phys = (void *)HHDM_TO_PHYS(virt_ret);

    struct PageMeta model = {.num_ref = 1,
                             .state = PAGE_STATE_TAKEN,
                             .type = PAGE_TYPE_ANONYMOUS};
    page_meta_set(phys, &model, alloc_size);

    UPDATE_STATS(allocated, +alloc_size);
    UPDATE_STATS(free, -alloc_size);

    if (out_alloc_size != NULL) *out_alloc_size = alloc_size;

    return phys;
}

void *pmm_alloczn_max(usize pages, usize *out_alloc_size) {
    return pmm_allocn_max(pages, out_alloc_size);
}

void pmm_hold(void *page) {
    struct PageMeta *page_meta = page_meta_get(page);
    assert(page_meta->state != PAGE_STATE_FREE);
    __atomic_add_fetch(&page_meta->num_ref, 1, __ATOMIC_RELAXED);
}

bool pmm_is_shared(void *page) {
    struct PageMeta *page_meta = page_meta_get(page);
    assert(page_meta->state != PAGE_STATE_FREE);
    return __atomic_load_n(&page_meta->num_ref, __ATOMIC_ACQUIRE) > 1;
}

void pmm_free(void *page) {
    struct PageMeta *page_meta = page_meta_get(page);

    if (page_meta->state != PAGE_STATE_TAKEN ||
        __atomic_add_fetch(&page_meta->num_ref, -1, __ATOMIC_ACQ_REL) != 0) {
        return;
    }

    // TODO: reimplement free
}

void pmm_freen(void *base, usize pages) {
    for (usize i = 0; i < pages; i++) pmm_free((u8 *)base + i * PAGE_SIZE);
}

void pmm_unreserven(void *base, usize pages) {
    struct PageMeta *page_meta_base = page_meta_get(base);
    struct FreelistEntry *entry = (void *)PHYS_TO_HHDM(base);
    memset(entry, 0, pages * PAGE_SIZE);
    memset(page_meta_base, 0, pages * sizeof(struct PageMeta));
    entry->pages = pages;

    FREELIST_LOCK(&g_main_free_list);
    freelist_add_ordered_nolock(&g_main_free_list, entry);
    FREELIST_UNLOCK(&g_main_free_list);

    UPDATE_STATS(reserved, -pages);
    UPDATE_STATS(free, +pages);
}

void pmm_get_stats(PMMStats *out_pmm_stats) { *out_pmm_stats = g_memory_stats; }

void pmm_init_threads(void) {
    log("TODO: reimplement pmm threads");
    // TODO reimplement pmm threads
}

void pmm_init(void) {
    log("initializing pmm");
    MemMap *memmap = info_get_memmap();
    uptr highest_addr = 0;
    uptr highest_indexed_addr = 0;
    TAILQ_INIT(&g_main_free_list.head);

    // Setup the fake entry in the page meta array
    g_page_meta.fake_entry.type = PAGE_TYPE_ANONYMOUS;
    g_page_meta.fake_entry.state = PAGE_STATE_RESERVED;
    g_page_meta.fake_entry.num_ref = 1;

    // Calculate memory map & page meta array size
    for (u64 i = 0; i < memmap->entry_count; i++) {
        MemMapEntry *entry = memmap->entries[i];
        g_memory_stats.total += entry->length;
        uptr seg_top = entry->base + entry->length;

        log_debug("segment: base=%p top=%p usable=%zu length=%zu", entry->base,
                  seg_top, memmap_is_usable(entry), entry->length);

        highest_addr = max(highest_addr, seg_top);

        if (memmap_is_reclaimable(entry)) {
            highest_indexed_addr = max(highest_indexed_addr, seg_top);
        }

        if (!memmap_is_usable(entry)) {
            g_memory_stats.reserved += entry->length;
            continue;
        }

        memset((void *)PHYS_TO_HHDM(entry->base), 0, entry->length);
        g_memory_stats.usable += entry->length;
        highest_indexed_addr = max(highest_indexed_addr, seg_top);
    }

    g_memory_stats.total = highest_addr;
    log_debug("searched all segments, found highest address at %p",
              highest_addr);

    // Compute the actual size of the page meta array
    usize page_meta_size = PAGE_META_ARRAY_SIZE(highest_indexed_addr);
    usize page_meta_pages = (page_meta_size + PAGE_SIZE - 1) / PAGE_SIZE;
    usize page_meta_aligned_size = page_meta_pages * PAGE_SIZE;
    log_debug("page meta array size = %zu byte(s) = %zu page(s) = %zu aligned "
              "byte(s)",
              page_meta_size, page_meta_pages, page_meta_aligned_size);

    // Find a segment that fits the array and allocate it there
    for (u64 i = 0; i < memmap->entry_count; i++) {
        MemMapEntry *entry = memmap->entries[i];

        if (!memmap_is_usable(entry)) continue;

        if (g_page_meta.buffer == NULL &&
            entry->length >= page_meta_aligned_size) {
            g_page_meta.buffer = (void *)PHYS_TO_HHDM(entry->base);
            g_page_meta.len = PAGE_META_ARRAY_LEN(highest_indexed_addr);
            g_page_meta.reserved_start = PHYS_TO_META_INDEX(
                highest_indexed_addr);
            g_page_meta.extended_len = PAGE_META_ARRAY_LEN(highest_addr);
            // Already zeroed before. Remember, 0 == PAGE_STATE_FREE

            // We now reserve the pages used for this array
            struct PageMeta model = {.num_ref = 1,
                                     .state = PAGE_STATE_RESERVED};
            page_meta_set((void *)entry->base, &model, page_meta_pages);
            g_memory_stats.usable -= page_meta_aligned_size;
            g_memory_stats.reserved += page_meta_aligned_size;

            // We also add this to the freelist. This list entry of couse is
            // a little special since a part of it has been reserved. The
            // address is rounded down to the nearest fully free page
            // boundary to guarantee that allocations will always return
            // aligned addresses. This is only performed if there is still
            // at least one page elft
            if (entry->length >= page_meta_aligned_size + PAGE_SIZE) {
                usize remaining_length = entry->length - page_meta_aligned_size;
                usize remaining_pages = remaining_length / PAGE_SIZE;

                struct FreelistEntry
                    *list_entry = (void *)(PHYS_TO_HHDM(entry->base) +
                                           page_meta_aligned_size);
                list_entry->pages = remaining_pages;
                freelist_add_ordered_nolock(&g_main_free_list, list_entry);
            }

            log_debug("page meta array: base=%p, segment=%zu, length=%zu",
                      g_page_meta.buffer, i, g_page_meta.len);
            continue;
        }

        // If we got here, it means the entire entry is free for us!
        struct FreelistEntry *list_entry = (void *)(PHYS_TO_HHDM(entry->base));
        list_entry->pages = entry->length / PAGE_SIZE;
        freelist_add_ordered_nolock(&g_main_free_list, list_entry);
    }

    log_debug("reserving remaining unusable pages");

    // We couldn't reserve all unusable regions before because the array
    // might not have been set yet. But we can not
    for (u64 i = 0; i < memmap->entry_count; i++) {
        MemMapEntry *entry = memmap->entries[i];

        if (!memmap_is_usable(entry) &&
            entry->base + entry->length < highest_indexed_addr) {
            struct PageMeta model = {.num_ref = 1,
                                     .state = PAGE_STATE_RESERVED};
            page_meta_set((void *)entry->base, &model,
                          (entry->length + PAGE_SIZE - 1) / PAGE_SIZE);
        }
    }

    g_memory_stats.free = g_memory_stats.usable;

    struct FreelistEntry *e, *_;
    TAILQ_FOREACH_SAFE(e, &g_main_free_list.head, tqe, _) {
        log_debug("freelist entry %p has %zu pages", e, e->pages);
    }
}
