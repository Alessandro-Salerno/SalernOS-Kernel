#include <arch/cpu.h>
#include <kernel/com/crt.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/failnth.h>
#include <kernel/com/util.h>
#include <kernel/platform/info.h>
#include <vendor/tailq.h>

// TODO: the exact-size lists are a patchy solution to a deeper problem caused
// by best-fit. Global lock hold times baloon without them in my benchmarks
// because the cahced-pionter refetch starts to dominate as smalls-size lists
// become longer and longer due to fragmentation. Fragmentation itself is not a
// major concern, as tests have shown it is not too bad, but even then it leaves
// uite a few small runs. This fixes the problem, though it has an arbitrary
// ceiling which probably shows in some workloads, so a good-fit alternative on
// the lines of TLSF would probably be better

// Runs of up to CONFIG_PMM_EXACT_LISTS pages each have a freelist of their own.
// Longer runs share a freelist with all runs whose length has the same bit
// width, up to the 32 bits of block_num_pages
#define FIRST_RANGE_LEN   (CONFIG_PMM_EXACT_LISTS + 1)
#define FIRST_RANGE_WIDTH (64 - __builtin_clzll(FIRST_RANGE_LEN))
#define NUM_FREELISTS     (CONFIG_PMM_EXACT_LISTS + 33 - FIRST_RANGE_WIDTH)

_Static_assert(CONFIG_PMM_EXACT_LISTS > 0, "there must be an exact freelist");
_Static_assert((FIRST_RANGE_LEN & (FIRST_RANGE_LEN - 1)) == 0,
               "the first range must start a new bit width");
_Static_assert(NUM_FREELISTS <= 64, "every freelist needs a bit in g_bitmap");

typedef struct Page Page;
typedef TAILQ_HEAD(FreelistHead, Page) FreelistHead;

typedef struct Freelist {
    FreelistHead head;
    Page *smallest_run;
    Page *largest_run;
} Freelist;

typedef struct CpuPageSet {
    FreelistHead main_head;
    usize main_num_pages;
    FreelistHead extra_head;
    usize extra_num_pages;
} CpuPageSet;

typedef enum PageState {
    PAGE_STATE_FREE = 0,
    PAGE_STATE_RESERVED,
    PAGE_STATE_ALLOCATED
} PageState;

// Pages cached by CPU page sets are allocated, but borrow the free fields
struct Page {
    PageState state;
    union {
        struct {
            usize num_ref;
        } allocated;
        struct {
            TAILQ_ENTRY(Page) link;
            u32 block_start;
            u32 block_num_pages;
        } free;
    };
};

static CpuPageSet g_cpu_page_sets[CONFIG_CPU_MAX];

static Spinlock g_pmm_lock = SPINLOCK_NEW();
static Freelist g_freelists[NUM_FREELISTS];
static u64 g_bitmap = 0;
static usize g_num_free_pages = 0;
static usize g_num_usable_pages = 0;
static usize g_num_reserved_pages = 0;
static usize g_num_total_pages = 0;
static struct {
    Page *array;
    usize real_len;
    Page fake_entry;
} g_pfn_db;

// PFN HELPERS

static bool pfn_is_fake(u32 pfn) { return pfn >= g_pfn_db.real_len; }
static Page *pfn_get(u32 pfn) {
    if (pfn < g_pfn_db.real_len) return g_pfn_db.array + pfn;
    else return &g_pfn_db.fake_entry;
}
static void *pfn_to_hhdm(u32 pfn) {
    return (void *)PHYS_TO_HHDM((uptr)pfn * PAGE_SIZE);
}
static bool pfn_is_reserved(u32 pfn) {
    return pfn_is_fake(pfn) || pfn_get(pfn)->state == PAGE_STATE_RESERVED;
}
static bool pfn_is_allocated(u32 pfn) {
    return !pfn_is_fake(pfn) && pfn_get(pfn)->state == PAGE_STATE_ALLOCATED;
}

// PAGE HELPERS

static bool page_is_fake(Page *page) { return page == &g_pfn_db.fake_entry; }
static u32 page_to_pfn(Page *page) {
    assert(!page_is_fake(page));
    return (u32)(uptr)(page - g_pfn_db.array);
}
static bool page_is_reserved(Page *page) {
    return page_is_fake(page) || page->state == PAGE_STATE_RESERVED;
}
static bool page_is_allocated(Page *page) {
    return !page_is_fake(page) && page->state == PAGE_STATE_ALLOCATED;
}
static bool page_is_free(Page *page) {
    return !page_is_fake(page) && page->state == PAGE_STATE_FREE;
}
static bool page_is_free_leader(Page *page) {
    assert(page_is_free(page));
    return page->free.block_start == page_to_pfn(page);
}
static bool page_is_free_edge(Page *page) {
    assert(page_is_free(page));
    assert(page->free.block_start <= page_to_pfn(page));
    return page_to_pfn(page) ==
        page->free.block_start + page->free.block_num_pages - 1;
}
static void page_make_free_leader(Page *page, u32 block_num_pages) {
    page->state = PAGE_STATE_FREE;
    page->free.block_start = page_to_pfn(page);
    page->free.block_num_pages = block_num_pages;
    assert(page_is_free_leader(page));
}
static void page_make_free_edge(Page *end_page, Page *leader_page) {
    assert(page_is_free_leader(leader_page));
    assert(page_to_pfn(end_page) >= page_to_pfn(leader_page));
    end_page->state = PAGE_STATE_FREE;
    end_page->free.block_start = page_to_pfn(leader_page);
    end_page->free.block_num_pages = leader_page->free.block_num_pages;
}

// PMM AND BLOCK HELPERS

static u64 fl_index(Freelist *fl);
static Freelist *fl_get_from_len(u32 num_pages);

static void pmm_lock(void) { spinlock_acq(&g_pmm_lock); }
static void pmm_unlock(void) { spinlock_rel(&g_pmm_lock); }
// Spinlocks are no-ops before the first thread exists
static bool pmm_is_locked(void) {
    return cpu_get_thread() == NULL || spinlock_is_held(&g_pmm_lock);
}

static Freelist *pmm_find_fit(usize num_pages) {
    assert(pmm_is_locked());
    assert(num_pages != 0);
    if (num_pages > UINT32_MAX) return NULL;

    u64 mask = ~((1ULL << fl_index(fl_get_from_len(num_pages))) - 1);

    u64 avail_lists = g_bitmap & mask;
    while (avail_lists != 0) {
        u64 n = __builtin_ffsll(avail_lists);
        assert(n != 0);
        assert(n <= NUM_FREELISTS);

        Freelist *fl = &g_freelists[n - 1];
        assert(!TAILQ_EMPTY(&fl->head));
        assert(fl->largest_run != NULL);
        assert(fl->smallest_run != NULL);

        if (fl->largest_run->free.block_num_pages >= num_pages) return fl;
        avail_lists &= avail_lists - 1;
    }

    return NULL;
}

static void pmm_make_free_block(u32 start_pfn, u32 block_len) {
    assert(pfn_is_allocated(start_pfn));
    assert(block_len > 0);

    u32 end_pfn = start_pfn + block_len - 1;
    assert(pfn_is_allocated(end_pfn));

    Page *start_page = pfn_get(start_pfn);
    Page *end_page = pfn_get(end_pfn);

    page_make_free_leader(start_page, block_len);
    page_make_free_edge(end_page, start_page);
}

static u32 pmm_split_block(u32 start_pfn, u32 right_start_idx) {
    assert(pmm_is_locked());
    assert(right_start_idx > 0);

    Page *start_page = pfn_get(start_pfn);
    assert(page_is_free(start_page));
    assert(page_is_free_leader(start_page));
    assert(right_start_idx < start_page->free.block_num_pages);

    // Shrink the left block to only reach [start_pfn, start_pfn +
    // right_start_idx)
    u32 block_num_pages = start_page->free.block_num_pages;
    start_page->free.block_num_pages = right_start_idx;

    // Create the edge of the left block
    u32 end_pfn = start_pfn + right_start_idx - 1;
    Page *end_page = pfn_get(end_pfn);
    page_make_free_edge(end_page, start_page);

    // Create the right-most block's leader
    u32 new_block_num_pages = block_num_pages - right_start_idx;
    u32 new_start_pfn = start_pfn + right_start_idx;
    Page *new_start_page = pfn_get(new_start_pfn);
    page_make_free_leader(new_start_page, new_block_num_pages);

    // Move the right-most end's leader to right_start_idx
    u32 new_end_pfn = start_pfn + block_num_pages - 1;
    Page *new_end_page = pfn_get(new_end_pfn);
    assert(page_is_free(new_end_page));
    page_make_free_edge(new_end_page, new_start_page);

    return new_start_pfn;
}

static u32 pmm_join_block(u32 start_pfn, u32 block_len, u32 *out_left_len,
                          u32 *out_right_len) {
    u32 end_pfn = start_pfn + block_len - 1;
    Page *start_page = pfn_get(start_pfn);
    Page *end_page = pfn_get(end_pfn);
    assert(page_is_free(start_page));
    assert(page_is_free_leader(start_page));
    assert(page_is_free(end_page));
    assert(page_is_free_edge(end_page));

    u32 new_start_pfn = start_pfn;
    u32 new_end_pfn = end_pfn;
    *out_left_len = 0;
    *out_right_len = 0;

    // Join left if there's another free block there
    if (start_pfn == 0) goto join_right;
    u32 prev_pfn = start_pfn - 1;
    Page *prev_page = pfn_get(prev_pfn);
    if (!page_is_free(prev_page)) goto join_right;
    if (!page_is_free_edge(prev_page)) goto join_right;
    u32 prev_leader_pfn = prev_page->free.block_start;
    assert(page_is_free_leader(pfn_get(prev_leader_pfn)));
    assert(new_start_pfn == start_pfn);
    new_start_pfn = prev_leader_pfn;
    *out_left_len = prev_page->free.block_num_pages;

join_right:
    // We join right if this is not the last real page. The composition alof
    // these two always joins full blocks and does not need to be recursively
    // repeated because of invariants
    if (end_pfn == g_pfn_db.real_len - 1) goto exit;
    u32 next_pfn = end_pfn + 1;
    Page *next_page = pfn_get(next_pfn);
    if (!page_is_free(next_page)) goto exit;
    if (!page_is_free_leader(next_page)) goto exit;
    u32 next_block_len = next_page->free.block_num_pages;
    assert(new_end_pfn == end_pfn);
    new_end_pfn += next_block_len;
    *out_right_len = next_block_len;

exit:
    assert(new_start_pfn <= start_pfn);
    assert(new_end_pfn >= end_pfn);

    Page *new_start_page = pfn_get(new_start_pfn);
    Page *new_end_page = pfn_get(new_end_pfn);
    u32 new_block_len = new_end_pfn - new_start_pfn + 1;
    page_make_free_leader(new_start_page, new_block_len);
    page_make_free_edge(new_end_page, new_start_page);

    return new_start_pfn;
}

static void pmm_punch_block(u32 start_pfn) {
    Page *start_page = pfn_get(start_pfn);
    assert(page_is_free_leader(start_page));

    u32 block_len = start_page->free.block_num_pages;
    Page model = {0};
    model.state = PAGE_STATE_ALLOCATED;
    model.allocated.num_ref = 1;

    // Benchmarks have shown thi to be a useful optiimzation, especially with
    // profiling enabled
    if (block_len == 1) *start_page = model;
    else memrepcpy(start_page, &model, sizeof(Page), block_len);
}

// FREE LISTS

static bool fl_invariants(Freelist *fl) {
    assert(fl != NULL);
    assert(fl >= g_freelists && fl <= &g_freelists[NUM_FREELISTS - 1]);
    assert(!(!TAILQ_EMPTY(&fl->head) &&
             (fl->smallest_run == NULL || fl->largest_run == NULL)));
    assert(TAILQ_EMPTY(&fl->head) ||
           fl->smallest_run->free.block_num_pages <=
               fl->largest_run->free.block_num_pages);
    return true;
}

static void fl_init(Freelist *fl) {
    TAILQ_INIT(&fl->head);
    fl->smallest_run = NULL;
    fl->largest_run = NULL;
    assert(fl_invariants(fl));
}

static u64 fl_index(Freelist *fl) { return fl - g_freelists; }
static u64 fl_lower_bound(Freelist *fl) {
    u64 index = fl_index(fl);
    if (index < CONFIG_PMM_EXACT_LISTS) return index + 1;
    return (u64)FIRST_RANGE_LEN << (index - CONFIG_PMM_EXACT_LISTS);
}
static u64 fl_upper_bound(Freelist *fl) {
    if (fl_index(fl) != NUM_FREELISTS - 1) return fl_lower_bound(fl + 1) - 1;
    else return UINT64_MAX;
}

static void fl_empty(Freelist *fl) {
    fl->smallest_run = fl->largest_run = NULL;
    g_bitmap &= ~(1ULL << fl_index(fl));
    assert(fl_invariants(fl));
}

static Freelist *fl_get_from_len(u32 num_pages) {
    assert(num_pages > 0);
    if (num_pages <= CONFIG_PMM_EXACT_LISTS) return &g_freelists[num_pages - 1];
    usize width = 64 - __builtin_clzg((u64)num_pages);
    return &g_freelists[CONFIG_PMM_EXACT_LISTS + width - FIRST_RANGE_WIDTH];
}

static Freelist *fl_get_from_entry(Page *entry) {
    assert(entry != NULL);
    return fl_get_from_len(entry->free.block_num_pages);
}

static void fl_refetch_cached_pointers(Freelist *fl) {
    assert(fl != NULL);

    Page *smallest_entry = NULL;
    usize smallest_num_pages = 0;
    Page *largest_entry = NULL;
    usize largest_num_pages = 0;

    Page *entry;
    TAILQ_FOREACH(entry, &fl->head, free.link) {
        if (entry->free.block_num_pages < smallest_num_pages ||
            smallest_entry == NULL) {
            smallest_entry = entry;
            smallest_num_pages = entry->free.block_num_pages;
        }
        if (entry->free.block_num_pages > largest_num_pages) {
            largest_entry = entry;
            largest_num_pages = entry->free.block_num_pages;
        }
        if (smallest_num_pages == fl_lower_bound(fl) &&
            largest_num_pages == fl_upper_bound(fl))
            break;
    }

    assert(!((smallest_entry != NULL) ^ (largest_entry != NULL)));
    assert(smallest_entry == NULL ||
           smallest_entry->free.block_num_pages == smallest_num_pages);
    assert(smallest_entry == NULL ||
           smallest_entry->free.block_num_pages <=
               largest_entry->free.block_num_pages);
    assert(largest_entry == NULL ||
           largest_entry->free.block_num_pages == largest_num_pages);

    fl->smallest_run = smallest_entry;
    fl->largest_run = largest_entry;
    if (largest_entry == NULL) fl_empty(fl);
}

// We don't assume invariants hold because we have to restore them
// The fixup only needs to run if the update touched one of the cached values:
//      - Shrinking only needs to update cached pointer if the entry being
//      shrunk is the largest entry. We apply a couple of fast paths to this
//      end, but the guarantee ramains
//      - Removing needs to update cached pointers whenever at least one is
//      affected. Fast paths exist for this as well, but the guarantee remains
static void fl_shrink_fixup(Freelist *fl, Page *updated, bool removed) {
    assert(fl != NULL);
    assert(updated != NULL);
    assert(fl->smallest_run != NULL);
    assert(fl->largest_run != NULL);

    // Regardless of whether we're removing or shrinking an entry, there's no
    // need to do any of this if neither cached pointer is affected
    if (updated != fl->smallest_run && updated != fl->largest_run) goto exit;

    // The we catch the case in which update is not equal to both but only to
    // one, yet both have the same size, so they can just alias
    assert(updated == fl->smallest_run || updated == fl->largest_run);
    if (removed && fl->smallest_run != fl->largest_run &&
        fl->smallest_run->free.block_num_pages ==
            fl->largest_run->free.block_num_pages) {
        if (updated == fl->smallest_run) fl->smallest_run = fl->largest_run;
        else fl->largest_run = fl->smallest_run;
        goto exit;
    }

    if (!removed && updated != fl->largest_run) goto exit;
    if (!removed &&
        TAILQ_FIRST(&fl->head) == TAILQ_LAST(&fl->head, FreelistHead)) {
        assert(updated == fl->largest_run);
        goto exit;
    }

    fl_refetch_cached_pointers(fl);

exit:
    assert(fl_invariants(fl));
}

static void fl_grow_fixup(Freelist *fl, Page *updated) {
    assert(fl != NULL);
    assert(updated != NULL);

    if (fl->largest_run == NULL) {
        assert(fl->smallest_run == NULL);
        assert(TAILQ_FIRST(&fl->head) == updated);
        assert(TAILQ_NEXT(updated, free.link) == NULL);
        fl->smallest_run = fl->largest_run = updated;
        g_bitmap |= (1ULL << fl_index(fl));
        goto exit;
    }

    assert(fl->smallest_run != NULL);
    assert(fl->largest_run != NULL);
    assert(!TAILQ_EMPTY(&fl->head));

    if (updated->free.block_num_pages > fl->largest_run->free.block_num_pages)
        fl->largest_run = updated;

    if (updated->free.block_num_pages <
        fl->smallest_run->free.block_num_pages) {
        fl->smallest_run = updated;
        goto exit;
    }

    // In the case in which the entry is not new  but has just grown, we have to
    // rescan only if the entry was the smallest
    if (updated == fl->smallest_run) fl_refetch_cached_pointers(fl);

exit:
    assert(fl_invariants(fl));
}

static void fl_remove_entry(Freelist *fl, Page *entry) {
    TAILQ_REMOVE(&fl->head, entry, free.link);
    fl_shrink_fixup(fl, entry, true);
}

static void fl_insert_entry(Freelist *fl, Page *entry) {
    TAILQ_INSERT_TAIL(&fl->head, entry, free.link);
    fl_grow_fixup(fl, entry);
}

static Freelist *fl_bubble_down(Freelist *fl, Page *entry) {
    Freelist *last = NULL;
    Freelist *curr = fl;
    while (entry->free.block_num_pages <= fl_upper_bound(curr)) {
        last = curr;
        if (curr != &g_freelists[0]) curr--;
        else break;
    }

    assert(last != NULL);
    if (last == fl) {
        fl_shrink_fixup(fl, entry, false);
        return last;
    }

    assert(fl_invariants(last));
    assert(fl_lower_bound(last) <= entry->free.block_num_pages &&
           entry->free.block_num_pages <= fl_upper_bound(last));

    fl_remove_entry(fl, entry);
    fl_insert_entry(last, entry);
    return last;
}

static Freelist *fl_bubble_up(Freelist *fl, Page *entry) {
    Freelist *last = NULL;
    Freelist *curr = fl;
    while (entry->free.block_num_pages >= fl_lower_bound(curr)) {
        last = curr;
        if (curr != &g_freelists[NUM_FREELISTS - 1]) curr++;
        else break;
    }

    assert(last != NULL);
    if (last == fl) {
        fl_grow_fixup(fl, entry);
        return last;
    }

    assert(fl_invariants(last));
    assert(fl_lower_bound(last) <= entry->free.block_num_pages &&
           entry->free.block_num_pages <= fl_upper_bound(last));

    fl_remove_entry(fl, entry);
    fl_insert_entry(last, entry);
    return last;
}

static Page *fl_shrink(Freelist *fl, Page *entry, usize shrink_by) {
    assert(fl != NULL);
    assert(entry != NULL);
    assert(shrink_by > 0);
    assert(shrink_by <= entry->free.block_num_pages);
    assert(fl_invariants(fl));
    __atomic_store_n(&g_num_free_pages, g_num_free_pages - shrink_by,
                     __ATOMIC_RELAXED);

    u32 num_pages = entry->free.block_num_pages;
    if (shrink_by == num_pages) {
        fl_remove_entry(fl, entry);
        return entry;
    }

    // Splitting the block shrinks the entry, which may then move to a lower
    // freelist
    u32 right_leader_pfn = pmm_split_block(page_to_pfn(entry),
                                           num_pages - shrink_by);
    fl_bubble_down(fl, entry);

    // Build the new, unlinked entry
    Page *right_entry = pfn_get(right_leader_pfn);
    right_entry->free.link.tqe_prev = NULL;
    right_entry->free.link.tqe_next = NULL;
    assert(right_entry->free.block_num_pages == shrink_by);

    assert(fl_invariants(fl));
    return right_entry;
}

// The caller has already grown the block, so only the freelist may change.
// Until the bubble-up, fl's cached pointers may see the new length
static void fl_grow(Freelist *fl, Page *entry) {
    assert(fl != NULL);
    assert(entry != NULL);
    fl_bubble_up(fl, entry);
}

static Page *fl_alloc(Freelist *fl, usize num_pages) {
    assert(fl_invariants(fl));
    assert(num_pages > 0);

    if (TAILQ_EMPTY(&fl->head)) return NULL;
    assert(fl->largest_run->free.block_num_pages >= num_pages);

    if (fl->smallest_run->free.block_num_pages >= num_pages) {
        return fl_shrink(fl, fl->smallest_run, num_pages);
    }

    Page *entry, *_;
    Page *found = NULL;
    usize found_pages = 0;
    TAILQ_FOREACH_SAFE(entry, &fl->head, free.link, _) {
        if (entry->free.block_num_pages >= num_pages &&
            (found == NULL || entry->free.block_num_pages < found_pages)) {
            found = entry;
            found_pages = entry->free.block_num_pages;
        }
        if (found_pages == num_pages || found_pages == fl_lower_bound(fl))
            break;
    }

    assert(found != NULL);
    return fl_shrink(fl, found, num_pages);
}

// This doesn't take a freelit, see below
// There's a necessary asymmetry between fl_free and fl_aloc: fl_alloc leaves
// fl_shrink to split blocks, whereas here we create and join the block
// ourselves before possibly handing it over to fl_grow. The reason is that
// every free needs to attempt a join to keep the global invariant, but not
// every free will end up growing an existing entry. Meanwhile, allocating from
// a freelist necessarily means shrinking an existing entry, and thus splitting
// an existing block
static void fl_free(Page *entry, u32 num_pages) {
    assert(entry != NULL);
    assert(num_pages > 0);
    __atomic_store_n(&g_num_free_pages, g_num_free_pages + num_pages,
                     __ATOMIC_RELAXED);

    u32 entry_pfn = page_to_pfn(entry);
    u32 entry_end_pfn = entry_pfn + num_pages - 1;

    // This is a bit ugly here, but doing it in the block helpers would be
    // uglier: in order to maintain freelist invariants, we have to remove what
    // will woon be the "right-join" from its freelist, otherwise state could
    // appear corrupted
    u32 unlinked_len = 0;
    if (entry_end_pfn != g_pfn_db.real_len - 1) {
        Page *next_page = pfn_get(entry_end_pfn + 1);
        if (page_is_free(next_page) && page_is_free_leader(next_page)) {
            unlinked_len = next_page->free.block_num_pages;
            fl_remove_entry(fl_get_from_entry(next_page), next_page);
        }
    }

    pmm_make_free_block(entry_pfn, num_pages);
    u32 left_len, right_len;
    u32 block_start_pfn = pmm_join_block(entry_pfn, num_pages, &left_len,
                                         &right_len);
    assert(right_len == unlinked_len);

    // If a block was joined to the left, it now covers the center and right.
    // Its freelist can be obtained through its old length, and the the
    // bubble-up does the rest
    if (left_len != 0) {
        assert(block_start_pfn != entry_pfn);
        fl_grow(fl_get_from_len(left_len), pfn_get(block_start_pfn));
        return;
    }

    // The entry is in no freelist, but a rightward join may have grown it
    assert(block_start_pfn == entry_pfn);
    fl_insert_entry(fl_get_from_entry(entry), entry);
}

// CPU PAGE SETS

static void cpups_init(CpuPageSet *cpu_ps) {
    TAILQ_INIT(&cpu_ps->main_head);
    TAILQ_INIT(&cpu_ps->extra_head);
    cpu_ps->main_num_pages = 0;
    cpu_ps->extra_num_pages = 0;
}

static Page *cpups_refill(CpuPageSet *cpu_ps) {
    assert(cpu_ps->main_num_pages == 0);
    assert(cpu_ps->extra_num_pages == 0);

    pmm_lock();
    while (cpu_ps->main_num_pages < CONFIG_PMM_CPU_PAGE_HIGH) {
        Freelist *fl = pmm_find_fit(1);
        if (fl == NULL) break;

        // Worst case, this is just like allocating one page at a time. Best
        // case, this contributes significantly to improving refill performance
        while (cpu_ps->main_num_pages < CONFIG_PMM_CPU_PAGE_HIGH &&
               !TAILQ_EMPTY(&fl->head)) {
            usize take = min(CONFIG_PMM_CPU_PAGE_HIGH - cpu_ps->main_num_pages,
                             fl->smallest_run->free.block_num_pages);
            Page *entry = fl_alloc(fl, take);
            assert(entry->free.block_num_pages == take);
            pmm_punch_block(page_to_pfn(entry));
            entry->free.block_num_pages = take;
            TAILQ_INSERT_TAIL(&cpu_ps->main_head, entry, free.link);
            cpu_ps->main_num_pages += take;
        }
    }
    pmm_unlock();

    return TAILQ_FIRST(&cpu_ps->main_head);
}

static bool cpups_alloc(u32 *out_pfn) {
    assert(out_pfn != NULL);

    spinlock_fake_acq();
    usize cpu_id = cpu_get_id();
    assert(cpu_id < CONFIG_CPU_MAX);

    CpuPageSet *cpu_ps = &g_cpu_page_sets[cpu_id];
    assert(cpu_ps->main_num_pages <= CONFIG_PMM_CPU_PAGE_HIGH);
    assert(cpu_ps->main_num_pages + cpu_ps->extra_num_pages <=
           CONFIG_PMM_CPU_PAGE_MAX);

    FreelistHead *head = NULL;
    Page *entry = NULL;

    if (cpu_ps->main_num_pages > 0) {
        entry = TAILQ_FIRST(&cpu_ps->main_head);
        assert(entry != NULL);
        cpu_ps->main_num_pages--;
        head = &cpu_ps->main_head;
    } else if (cpu_ps->extra_num_pages > 0) {
        entry = TAILQ_FIRST(&cpu_ps->extra_head);
        assert(entry != NULL);
        cpu_ps->extra_num_pages--;
        head = &cpu_ps->extra_head;
    } else if ((entry = cpups_refill(cpu_ps)) != NULL) {
        cpu_ps->main_num_pages--;
        head = &cpu_ps->main_head;
    }

    if (entry != NULL) {
        if (entry->free.block_num_pages > 1) {
            Page *sub_entry = entry + entry->free.block_num_pages - 1;
            entry->free.block_num_pages--;
            entry = sub_entry;
        } else TAILQ_REMOVE(head, entry, free.link);
    }

    spinlock_fake_rel();

    if (entry != NULL) {
        entry->allocated.num_ref = 1;
        *out_pfn = page_to_pfn(entry);
        return true;
    }

    return false;
}

static void cpups_free(u32 start_pfn, u32 block_len) {
    spinlock_fake_acq();
    usize cpu_id = cpu_get_id();
    assert(cpu_id < CONFIG_CPU_MAX);

    CpuPageSet *cpu_ps = &g_cpu_page_sets[cpu_id];
    assert(cpu_ps->main_num_pages <= CONFIG_PMM_CPU_PAGE_HIGH);
    assert(cpu_ps->main_num_pages + cpu_ps->extra_num_pages <=
           CONFIG_PMM_CPU_PAGE_MAX);

    usize to_main = min(block_len,
                        CONFIG_PMM_CPU_PAGE_HIGH - cpu_ps->main_num_pages);
    if (to_main > 0) {
        Page *entry = pfn_get(start_pfn);
        entry->free.block_num_pages = to_main;

        TAILQ_INSERT_TAIL(&cpu_ps->main_head, entry, free.link);
        cpu_ps->main_num_pages += to_main;

        start_pfn += to_main;
        block_len -= to_main;
    }

    if (block_len == 0) goto exit;
    assert(cpu_ps->main_num_pages == CONFIG_PMM_CPU_PAGE_HIGH);

    Page *entry = pfn_get(start_pfn);
    entry->free.block_num_pages = block_len;

    if (cpu_ps->main_num_pages + cpu_ps->extra_num_pages + block_len <=
        CONFIG_PMM_CPU_PAGE_MAX) {
        TAILQ_INSERT_TAIL(&cpu_ps->extra_head, entry, free.link);
        cpu_ps->extra_num_pages += block_len;
        goto exit;
    }

    pmm_lock();
    fl_free(entry, block_len);
    while ((entry = TAILQ_FIRST(&cpu_ps->extra_head))) {
        TAILQ_REMOVE_HEAD(&cpu_ps->extra_head, free.link);
        u32 entry_len = entry->free.block_num_pages;
        cpu_ps->extra_num_pages -= entry_len;
        fl_free(entry, entry_len);
    }
    pmm_unlock();

exit:
    spinlock_fake_rel();
}

// PMM HELPERS THAT NEED TO BE HERE

static void pmm_release_block(u32 start_pfn, u32 block_len) {
    // fl_free expects allocated ranges, so we'll give it what it wants
    Page model = {0};
    model.state = PAGE_STATE_ALLOCATED;
    memrepcpy(pfn_get(start_pfn), &model, sizeof(Page), block_len);
    __atomic_add_fetch(&g_num_usable_pages, block_len, __ATOMIC_RELAXED);

    pmm_lock();
    fl_free(pfn_get(start_pfn), block_len);
    pmm_unlock();
}

// PUBLIC API

void *pmm_allocn(usize pages) {
    assert(pages > 0);
    if (failnth_should_fail()) return NULL;

    if (pages == 1) {
        u32 pfn;
        if (!cpups_alloc(&pfn)) return NULL;
        return (void *)((uptr)pfn * PAGE_SIZE);
    }

    pmm_lock();
    Freelist *fl = pmm_find_fit(pages);
    if (fl == NULL) {
        pmm_unlock();
        return NULL;
    }

    Page *entry = fl_alloc(fl, pages);
    assert(entry != NULL);
    // Punching overwrites the free fields
    assert(entry->free.block_num_pages == pages);
    pmm_punch_block(page_to_pfn(entry));
    pmm_unlock();

    return (void *)((uptr)page_to_pfn(entry) * PAGE_SIZE);
}

void *pmm_alloczn(usize pages) {
    void *ret = pmm_allocn(pages);
    if (ret != NULL) memset((void *)PHYS_TO_HHDM(ret), 0, pages * PAGE_SIZE);
    return ret;
}

void *pmm_alloc(void) { return pmm_allocn(1); }
void *pmm_allocz(void) { return pmm_alloczn(1); }

usize pmm_alloczn_arr(usize num_pages, void **out_pages) {
    assert(out_pages != NULL);

    usize successful = 0;
    for (usize i = 0; i < num_pages; i++) {
        void *ret = pmm_allocz();
        if (ret == NULL) break;
        out_pages[i] = ret;
        successful++;
    }

    return successful;
}

void *pmm_allocz_reserved(void) {
    void *ret = pmm_allocz();
    if (ret == NULL) return NULL;
    u32 pfn = (uptr)ret / PAGE_SIZE;
    Page *page = pfn_get(pfn);
    assert(page->state == PAGE_STATE_ALLOCATED);
    page->state = PAGE_STATE_RESERVED;
    __atomic_sub_fetch(&g_num_usable_pages, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&g_num_reserved_pages, 1, __ATOMIC_RELAXED);
    return ret;
}

// For multi-page contiguous allocations, we use the head page for refcounts.
// Dropping references on intermediate pages is undefined behavior

void pmm_hold(void *page) {
    u32 pfn = (uptr)page / PAGE_SIZE;
    Page *page_desc = pfn_get(pfn);
    if (page_is_reserved(page_desc)) return;
    assert(page_is_allocated(page_desc));
    __atomic_add_fetch(&page_desc->allocated.num_ref, 1, __ATOMIC_RELAXED);
}
bool pmm_is_shared(void *page) {
    u32 pfn = (uptr)page / PAGE_SIZE;
    Page *page_desc = pfn_get(pfn);
    assert(!page_is_free(page_desc));
    return page_is_reserved(page_desc) ||
        (page_is_allocated(page_desc) &&
         __atomic_load_n(&page_desc->allocated.num_ref, __ATOMIC_ACQUIRE) > 1);
}

void pmm_freen(void *base, usize pages) {
    u32 pfn = (u32)((uptr)base / PAGE_SIZE);
    Page *page = pfn_get(pfn);
    if (page_is_reserved(page)) return;
    if (__atomic_add_fetch(&page->allocated.num_ref, -1, __ATOMIC_ACQ_REL) ==
        0) {
        cpups_free(pfn, pages);
    }
}
void pmm_free(void *page) { pmm_freen(page, 1); }

void pmm_unreserven(void *base, usize pages) {
    u32 pfn = (u32)((uptr)base / PAGE_SIZE);
    assert(pages > 0);
    assert(!pfn_is_fake(pfn + pages - 1));
    assert(pfn_is_reserved(pfn) && pfn_is_reserved(pfn + pages - 1));

    memset(pfn_to_hhdm(pfn), 0, pages * PAGE_SIZE);
    __atomic_sub_fetch(&g_num_reserved_pages, pages, __ATOMIC_RELAXED);
    pmm_release_block(pfn, pages);
}

void pmm_get_stats(PMMStats *out_pmm_stats) {
    usize num_free_pages = __atomic_load_n(&g_num_free_pages, __ATOMIC_RELAXED);
    usize num_usable_pages = __atomic_load_n(&g_num_usable_pages,
                                             __ATOMIC_RELAXED);
    usize num_reserved_pages = __atomic_load_n(&g_num_reserved_pages,
                                               __ATOMIC_RELAXED);
    num_free_pages = min(num_free_pages, num_usable_pages);

    *out_pmm_stats = (PMMStats){
        .total = g_num_total_pages * PAGE_SIZE,
        .usable = num_usable_pages * PAGE_SIZE,
        .reserved = num_reserved_pages * PAGE_SIZE,
        .free = num_free_pages * PAGE_SIZE,
        .allocated = (num_usable_pages - num_free_pages) * PAGE_SIZE};
}

void pmm_init(void) {
    log("initializing pmm");

    // Initialize freelists
    for (usize i = 0; i < NUM_FREELISTS; i++) fl_init(&g_freelists[i]);
    for (usize i = 0; i < CONFIG_CPU_MAX; i++) cpups_init(&g_cpu_page_sets[i]);

    g_pfn_db.fake_entry.state = PAGE_STATE_RESERVED;
    g_pfn_db.fake_entry.allocated.num_ref = 1;

    // The PFN database only covers memory that is or may become usable,
    // everything above is backed by the fake entry
    MemMap *memmap = info_get_memmap();
    uptr highest_indexed_addr = 0;
    for (u64 i = 0; i < memmap->entry_count; i++) {
        MemMapEntry *entry = memmap->entries[i];
        uptr seg_top = entry->base + entry->length;
        log_debug("segment: base=%p top=%p usable=%zu length=%zu", entry->base,
                  seg_top, memmap_is_usable(entry), entry->length);
        g_num_total_pages = max(g_num_total_pages,
                                (seg_top + PAGE_SIZE - 1) / PAGE_SIZE);

        if (memmap_is_reclaimable(entry))
            highest_indexed_addr = max(highest_indexed_addr, seg_top);
        if (!memmap_is_usable(entry)) {
            g_num_reserved_pages += (entry->length + PAGE_SIZE - 1) / PAGE_SIZE;
            continue;
        }

        memset((void *)PHYS_TO_HHDM(entry->base), 0, entry->length);
        highest_indexed_addr = max(highest_indexed_addr, seg_top);
    }

    usize real_len = highest_indexed_addr / PAGE_SIZE;
    assert(real_len <= (usize)UINT32_MAX + 1);
    usize array_pages = (real_len * sizeof(Page) + PAGE_SIZE - 1) / PAGE_SIZE;

    MemMapEntry *array_entry = NULL;
    for (u64 i = 0; i < memmap->entry_count; i++) {
        MemMapEntry *entry = memmap->entries[i];
        if (!memmap_is_usable(entry)) continue;
        if (entry->length < array_pages * PAGE_SIZE) continue;
        array_entry = entry;
        break;
    }

    assert(array_entry != NULL);
    g_num_reserved_pages += array_pages;
    g_pfn_db.array = (void *)PHYS_TO_HHDM(array_entry->base);
    g_pfn_db.real_len = real_len;
    log_debug("pfn database: base=%p, len=%zu, pages=%zu", g_pfn_db.array,
              real_len, array_pages);

    // Everything starts reserved (including holes in the memory map and the
    // database itself), then usable segments are released
    Page model = {0};
    model.state = PAGE_STATE_RESERVED;
    model.allocated.num_ref = 1;
    memrepcpy(g_pfn_db.array, &model, sizeof(Page), real_len);

    for (u64 i = 0; i < memmap->entry_count; i++) {
        MemMapEntry *entry = memmap->entries[i];
        if (!memmap_is_usable(entry)) continue;

        u32 start_pfn = entry->base / PAGE_SIZE;
        u32 block_len = entry->length / PAGE_SIZE;
        if (entry == array_entry) {
            start_pfn += array_pages;
            block_len -= array_pages;
        }

        if (block_len > 0) pmm_release_block(start_pfn, block_len);
    }
}
