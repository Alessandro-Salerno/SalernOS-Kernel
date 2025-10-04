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
#include <errno.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/panic.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/info.h>
#include <lib/hashmap.h>
#include <lib/mem.h>
#include <lib/printf.h>
#include <lib/searchtree.h>
#include <lib/spinlock.h>
#include <lib/util.h>
#include <stdint.h>
#include <vendor/tailq.h>

#define FREELIST_LOCK(freelist_head) kspinlock_acquire(&(freelist_head)->lock);
#define FREELIST_UNLOCK(freelist_head) \
    kspinlock_release(&(freelist_head)->lock);

#define LAZYLIST_LOCK(lazylist_head) kspinlock_acquire(&(lazylist_head)->lock);
#define LAZYLIST_UNLOCK(lazylist_head) \
    kspinlock_release(&(lazylist_head)->lock);
#define LAZYLIST_WAIT(lazylist_head)                   \
    {                                                  \
        LAZYLIST_LOCK(lazylist_head);                  \
        com_sys_sched_wait(&(lazylist_head)->waitlist, \
                           &(lazylist_head)->lock);    \
        LAZYLIST_UNLOCK(lazylist_head);                \
    }
#define LAZYLIST_NOTIFY(lazylist_head)                    \
    {                                                     \
        LAZYLIST_LOCK(lazylist_head);                     \
        com_sys_sched_notify(&(lazylist_head)->waitlist); \
        LAZYLIST_UNLOCK(lazylist_head);                   \
    }

#define PHYS_TO_META_INDEX(page_phys) ((uintptr_t)(page_phys) / ARCH_PAGE_SIZE)
#define PAGE_META_ARRAY_LEN(memsize)  ((memsize) / ARCH_PAGE_SIZE)
#define PAGE_META_ARRAY_SIZE(memsize) \
    (PAGE_META_ARRAY_LEN(memsize) * sizeof(struct page_meta))

#define UPDATE_STATS(field, diff)            \
    __atomic_add_fetch(&MemoryStats.field,   \
                       diff *ARCH_PAGE_SIZE, \
                       __ATOMIC_SEQ_CST)
#define READ_STATS(field) __atomic_load_n(&MemoryStats.field, __ATOMIC_SEQ_CST)

// NOTE: the defrag thread is not implemented yet (see comments below), so to
// avoid warnings I mark those functions as used. But to avoid confusion on
// whether they're really used or not, I use this macro instead. You're welcome
#define DEFRAG_NOT_IMPLEMENTED KUSED

TAILQ_HEAD(freelist_tailq, freelist_entry);

struct freelist_head {
    struct freelist_tailq head;
    KSEARCHTREE_ROOT(, freelist_entry) tree;
    kspinlock_t lock;
};

// A block of one or more contigous pages that points to two more such blocks
// (one before, one after)
struct freelist_entry {
    TAILQ_ENTRY(freelist_entry) tqe;
    KSEARCHTREE_NODE(freelist_entry, uintptr_t) stn;
    size_t pages;
};

struct lazylist_head {
    struct lazylist_entry  *first;
    struct com_thread_tailq waitlist;
    kspinlock_t             lock;
};

// An entry into a lazy list, i.e. a list where each entry is a distinct page
// which is waiting for lazy processing. This struct is mapped directly over the
// page, using the first bytes of the buffer
struct lazylist_entry {
    struct lazylist_entry *next;
    size_t                 pages;
};

enum page_state {
    // NOTE: this must be zero, since all memory is zeroed on boot, and all page
    // structs in the array are assumed to report a free state
    E_PAGE_STATE_FREE = 0,
    E_PAGE_STATE_RESERVED,
    E_PAGE_STATE_TAKEN
};

enum page_type {
    E_PAGE_TYPE_ANONYMOUS,
    E_PAGE_TYPE_FILE
};

// An entry into the global page array that holds data about the page which is
// situated outside the page tiself. The index into the array is calculated as
// follows:
//      idx = page_phys_addr / ARCH_PAGE_SIZE
struct page_meta {
    enum page_state state;
    size_t          num_ref;

    enum page_type type;
    union {};
};

struct page_meta_array {
    struct page_meta *buffer;
    size_t            len;
};

// GLOBAL VARIABLES

// List of currently available blocks. This list is ordered by block size
static struct freelist_head MainFreeList = {0};
// List of pages to be zeroed by async thread
static struct lazylist_head ZeroLazyList = {0};
// List of pages that have been zeroed, but have yet to be inserted in the
// correct order in the main free list
static struct lazylist_head InsertLazyList = {0};
// Array of page metadata. Initialized by the init() function to piont to a the
// HHDM representation of a memory map entry large enough to hold it
static struct page_meta_array PageMeta = {0};
// Consolidated data for fast access. Should be accessed atomically after
// initialization
static com_pmm_stats_t MemoryStats = {0};
// Background threads may go to sleep at some point to save CPU time, so they
// need waitlists. But defrag is not on a lazylist, so it needs its own here
static struct com_thread_tailq DefragThreadWaitlist;
static kspinlock_t             DefragNotifyLock = KSPINLOCK_NEW();
// To help the defrag thread, the insert thread tires to group pages a little
static khashmap_t InsertThreadDefragMap;

// UTILITY FUNCTIONS

// Freelists are ordered, so insertion should be too
static inline void freelist_add_ordered_nolock(struct freelist_head  *freelist,
                                               struct freelist_entry *entry) {
    // Fast path for 1-page sized inserts. These will always go to the end
    if (1 == entry->pages) {
        struct freelist_entry *curr_last = TAILQ_LAST(&freelist->head,
                                                      freelist_tailq);
        // If the freelist is empty, we just add the entry
        if (NULL == curr_last) {
            goto fallback;
        }
        // If it comes right after the last entry, we merge the two
        if ((uintptr_t)entry ==
            (uintptr_t)curr_last + curr_last->pages * ARCH_PAGE_SIZE) {
            curr_last->pages++;
            return;
        }
        // If it comes right before, we do the same
        if ((uintptr_t)curr_last == (uintptr_t)entry + ARCH_PAGE_SIZE) {
            entry->pages += curr_last->pages;
            TAILQ_REMOVE(&freelist->head, curr_last, tqe);
        }
        // If the two entries exist, but are not contiguous, or the new entry is
        // contiguous but comes before (last if statement), we ad the new entryu
        // to the end
        goto fallback;
    }

    struct freelist_entry *curr, *_;
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
static inline void freelist_add_merged_nolock(struct freelist_head  *freelist,
                                              struct freelist_entry *entry) {
    uintptr_t entry_first_page = (uintptr_t)entry;
    uintptr_t entry_last_page  = entry_first_page +
                                (entry->pages - 1) * ARCH_PAGE_SIZE;

    struct freelist_entry *curr, *_;
    TAILQ_FOREACH_SAFE(curr, &freelist->head, tqe, _) {
        uintptr_t curr_first_page = (uintptr_t)curr;
        uintptr_t curr_last_page  = curr_first_page +
                                   (entry->pages - 1) * ARCH_PAGE_SIZE;

        if (curr_first_page == entry_last_page + ARCH_PAGE_SIZE) {
            entry->pages += curr->pages;
            TAILQ_INSERT_BEFORE(curr, entry, tqe);
            TAILQ_REMOVE(&freelist->head, curr, tqe);
            curr = entry;
            return;
        }

        if (curr_last_page == entry_first_page - ARCH_PAGE_SIZE) {
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
static inline struct freelist_entry *
freelist_pop_rear_nolock(struct freelist_head *freelist) {
    struct freelist_entry *last = TAILQ_LAST(&MainFreeList.head,
                                             freelist_tailq);
    KASSERT(NULL != last);

    if (1 == last->pages) {
        TAILQ_REMOVE(&freelist->head, last, tqe);
    } else {
        last->pages--;
        last = (void *)((uintptr_t)last + ARCH_PAGE_SIZE * last->pages);
    }

    return last;
}

// When allocating multiple pages, however, we take them from the front, because
// it is more likelly that we'll find a match immediatelly
static inline struct freelist_entry *
freelist_pop_front_nolock(size_t               *out_alloc_size,
                          struct freelist_head *freelist,
                          size_t                num_pages) {
    struct freelist_entry *ret = TAILQ_FIRST(&freelist->head);
    KASSERT(NULL != ret);
    size_t alloc_size = KMIN(num_pages, ret->pages);

    struct freelist_entry *entry      = ret;
    struct freelist_entry *next_entry = TAILQ_NEXT(entry, tqe);

    if (alloc_size == ret->pages) {
        TAILQ_REMOVE(&freelist->head, ret, tqe);
    } else {
        ret->pages -= alloc_size;
        ret = (void *)((uintptr_t)ret + ARCH_PAGE_SIZE * ret->pages);
    }

    // Since we decrement the number of pages in this entry, it may now longer
    // be the largest
    if (NULL != next_entry && next_entry->pages > entry->pages) {
        TAILQ_REMOVE(&freelist->head, entry, tqe);
        freelist_add_ordered_nolock(freelist, entry);
    }

    *out_alloc_size = alloc_size;
    return ret;
}

// Lazy lists are not ordered, this adds to the head of the list
static inline void lazylist_add_nolock(struct lazylist_head  *lazylist,
                                       struct lazylist_entry *entry) {
    if (NULL == lazylist->first) {
        goto fallback;
    }

    // Functiosns like memset scale better on larger blocks, so we try to merge
    // these
    if ((uintptr_t)entry ==
        (uintptr_t)lazylist->first + lazylist->first->pages * ARCH_PAGE_SIZE) {
        lazylist->first->pages++;
        return;
    }

    if ((uintptr_t)lazylist->first ==
        (uintptr_t)entry + entry->pages * ARCH_PAGE_SIZE) {
        entry->next = lazylist->first->next;
        entry->pages += lazylist->first->pages;
        *lazylist->first = (struct lazylist_entry){0};
        lazylist->first  = entry;
        return;
    }

fallback:
    entry->next     = lazylist->first;
    lazylist->first = entry;
}

// Take the lazy list as it is now,  then empty it. This way, the caller can
// take a local pointer to the entire list, ensuring that the global lock is
// held for as little as possible, but the contents are still thread-safe
static inline struct lazylist_entry *
lazylist_truncate_nolock(struct lazylist_head *lazylist) {
    struct lazylist_entry *ret = lazylist->first;
    lazylist->first            = NULL;
    return ret;
}

static inline struct page_meta *page_meta_get(void *phys_addr) {
    size_t index = PHYS_TO_META_INDEX(phys_addr);
    KASSERT(index < PageMeta.len);
    return &PageMeta.buffer[index];
}

static inline void
page_meta_set(void *page_phys, struct page_meta *tocopy, size_t num_pages) {
    uintptr_t end_addr = (uintptr_t)page_phys + num_pages * ARCH_PAGE_SIZE;
    size_t    index1   = PHYS_TO_META_INDEX(page_phys);
    size_t    index2   = PHYS_TO_META_INDEX(end_addr) - 1;
    KASSERT(index1 < PageMeta.len);
    KASSERT(index2 < PageMeta.len);

    kmemrepcpy(page_meta_get(page_phys),
               tocopy,
               sizeof(struct page_meta),
               num_pages);
}

// BACKGROUND TASKS

DEFRAG_NOT_IMPLEMENTED static void
pmm_notify_defrag_callout(com_callout_t *callout) {
    kspinlock_acquire(&DefragNotifyLock);
    com_sys_sched_notify(&DefragThreadWaitlist);
    kspinlock_release(&DefragNotifyLock);
    com_sys_callout_reschedule(callout,
                               CONFIG_PMM_DEFRAG_TIMEOUT / 1000UL *
                                   KNANOS_PER_SEC);
}

static void pmm_zero_thread(void) {
    bool                   already_done = false;
    struct lazylist_entry *to_zero      = NULL;
    size_t zero_max = READ_STATS(usable) / 10000 * CONFIG_PMM_ZERO_MAX /
                      ARCH_PAGE_SIZE;
    size_t to_insert_notify = CONFIG_PMM_NOTIFY_INSERT * ARCH_PAGE_SIZE;

    while (true) {
        // Allow kernel preemption
        com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
        curr_thread->lock_depth   = 0;
        ARCH_CPU_ENABLE_INTERRUPTS();

        if (already_done) {
            LAZYLIST_WAIT(&ZeroLazyList);
            already_done = false;
        }

        if (NULL == to_zero) {
            LAZYLIST_LOCK(&ZeroLazyList);
            to_zero = lazylist_truncate_nolock(&ZeroLazyList);
            LAZYLIST_UNLOCK(&ZeroLazyList);
        }

        // Repeated if because the values might have changed
        if (NULL == to_zero) {
            already_done = true;
            continue;
        }

        size_t pages_zeroed = 0;
        while (NULL != to_zero && pages_zeroed < zero_max) {
            struct lazylist_entry *next          = to_zero->next;
            size_t                 to_zero_pages = to_zero->pages;

            // Memset only the part the insert thread doesn't need
            kmemset(to_zero + 1,
                    to_zero_pages * ARCH_PAGE_SIZE -
                        sizeof(struct lazylist_entry),
                    0);

            // Pass it on to the insert thrtead
            LAZYLIST_LOCK(&InsertLazyList);
            lazylist_add_nolock(&InsertLazyList, to_zero);
            LAZYLIST_UNLOCK(&InsertLazyList);

            to_zero = next;
            pages_zeroed += to_zero_pages;
        }

        UPDATE_STATS(to_zero, -pages_zeroed);
        size_t to_insert = UPDATE_STATS(to_insert, +pages_zeroed);

        if (to_insert >= to_insert_notify) {
            LAZYLIST_NOTIFY(&InsertLazyList);
        }

        already_done = true;
    }
}

static void pmm_insert_thread(void) {
    bool                   already_done = false;
    struct lazylist_entry *to_insert    = NULL;
    size_t insert_max = READ_STATS(usable) / 10000 * CONFIG_PMM_ZERO_MAX /
                        ARCH_PAGE_SIZE;
    size_t to_defrag_notify = CONFIG_PMM_NOTIFY_DEFRAG * ARCH_PAGE_SIZE;

    while (true) {
        // Allow kernel preemption
        com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
        curr_thread->lock_depth   = 0;
        ARCH_CPU_ENABLE_INTERRUPTS();

        if (already_done) {
            LAZYLIST_WAIT(&InsertLazyList);
            already_done = false;
        }

        if (NULL == to_insert) {
            LAZYLIST_LOCK(&InsertLazyList);
            to_insert = lazylist_truncate_nolock(&InsertLazyList);
            LAZYLIST_UNLOCK(&InsertLazyList);
        }

        // Repeated if because the values might have changed
        if (NULL == to_insert) {
            already_done = true;
            continue;
        }

        // For each entry in the lazylist, we try to see if is contiguous with
        // some previsouly added block, so that we already do a bit of defrag
        // here
        size_t pages_inserted = 0;
        while (NULL != to_insert && pages_inserted < insert_max) {
            struct lazylist_entry *next            = to_insert->next;
            size_t                 to_insert_pages = to_insert->pages;
            uintptr_t              to_insert_addr  = (uintptr_t)to_insert;
            uintptr_t prev_page_addr = to_insert_addr - ARCH_PAGE_SIZE;
            uintptr_t next_page_addr = to_insert_addr +
                                       (to_insert_pages * ARCH_PAGE_SIZE);
            bool add_to_hashmap = true;

            struct lazylist_entry *prev_page_entry;
            struct lazylist_entry *next_page_entry;
            if (ENOENT != KHASHMAP_GET(&prev_page_entry,
                                       &InsertThreadDefragMap,
                                       &prev_page_addr)) {
                prev_page_entry->pages += to_insert_pages;
                to_insert      = prev_page_entry;
                to_insert_addr = prev_page_addr;
                add_to_hashmap = false;
            }

            if (ENOENT != KHASHMAP_GET(&next_page_entry,
                                       &InsertThreadDefragMap,
                                       &next_page_addr)) {
                KHASHMAP_REMOVE(&InsertThreadDefragMap, &next_page_addr);
                to_insert->pages += next_page_entry->pages;
                *next_page_entry = (struct lazylist_entry){0};
            }

            if (add_to_hashmap) {
                KHASHMAP_PUT(&InsertThreadDefragMap,
                             &to_insert_addr,
                             to_insert);
            }

            to_insert = next;
            pages_inserted += to_insert_pages;
        }

        // Perform the actual insert process
        KHASHMAP_FOREACH(&InsertThreadDefragMap) {
            struct lazylist_entry *lazylist_entry = entry->value;
            // Since we're using the address as key, but the address also
            // contains the structure, then we're left with this unreadable
            // mess. I'm sorry
            uintptr_t entry_key = (uintptr_t)entry->value;
            size_t    pages     = lazylist_entry->pages;
            // We transform it into a freelist entry
            struct freelist_entry *freelist_entry = entry->value;
            freelist_entry->pages                 = pages;

            // This is zeroeing, but we do it here because it is less fragmented
            // and kernel lib likes that. No need to lock this since as long as
            // the pages are not in the freelist, we're the only ones that will
            // look into this buffer
            struct page_meta *page_meta_base = page_meta_get(
                (void *)ARCH_HHDM_TO_PHYS(freelist_entry));
            kmemset(page_meta_base,
                    freelist_entry->pages * sizeof(struct page_meta),
                    0);

            FREELIST_LOCK(&MainFreeList);
            freelist_add_ordered_nolock(&MainFreeList, freelist_entry);
            FREELIST_UNLOCK(&MainFreeList);

            KHASHMAP_REMOVE(&InsertThreadDefragMap, &entry_key);
        }

        UPDATE_STATS(to_insert, -pages_inserted);
        size_t to_defrag = UPDATE_STATS(to_defrag, +pages_inserted);

        if (to_defrag >= to_defrag_notify) {
            kspinlock_acquire(&DefragNotifyLock);
            com_sys_sched_notify(&DefragThreadWaitlist);
            kspinlock_release(&DefragNotifyLock);
        }

        already_done = true;
    }
}

DEFRAG_NOT_IMPLEMENTED static void pmm_defrag_thread(void) {
    // TODO: the insert thread tries to defragment things, but it would be
    // better to also have this thread run every so often to fully defragment
    // the structure. Say, after a certain amount of time (see callout function
    // above), after a certain number of pages reinserted, or when the freelist
    // reaches a certain number of entries. To speed this up, I could use the
    // search tree (which is ordered by address instead of length) and perform
    // an in-order visit while merging nodes
}

// INTERFACE FUNCTIONS

void *com_mm_pmm_alloc(void) {
    FREELIST_LOCK(&MainFreeList);
    struct freelist_entry *virt_ret = freelist_pop_rear_nolock(&MainFreeList);
    FREELIST_UNLOCK(&MainFreeList);

    *virt_ret                   = (struct freelist_entry){0};
    void             *phys      = (void *)ARCH_HHDM_TO_PHYS(virt_ret);
    struct page_meta *page_meta = page_meta_get(phys);

    KASSERT(E_PAGE_STATE_FREE == page_meta->state);
    KASSERT(0 == page_meta->num_ref);
    page_meta->num_ref = 1;
    page_meta->state   = E_PAGE_STATE_TAKEN;
    page_meta->type    = E_PAGE_TYPE_ANONYMOUS;

    UPDATE_STATS(used, +1);
    UPDATE_STATS(free, -1);

    return phys;
}

void *com_mm_pmm_alloc_many(size_t pages) {
    if (1 == pages) {
        return com_mm_pmm_alloc();
    }

    FREELIST_LOCK(&MainFreeList);
    size_t                 alloc_size;
    struct freelist_entry *virt_ret = freelist_pop_front_nolock(&alloc_size,
                                                                &MainFreeList,
                                                                pages);
    FREELIST_UNLOCK(&MainFreeList);
    KASSERT(alloc_size == pages);

    *virt_ret  = (struct freelist_entry){0};
    void *phys = (void *)ARCH_HHDM_TO_PHYS(virt_ret);

    struct page_meta model = {.num_ref = 1,
                              .state   = E_PAGE_STATE_TAKEN,
                              .type    = E_PAGE_TYPE_ANONYMOUS};
    page_meta_set(phys, &model, pages);

    UPDATE_STATS(used, +pages);
    UPDATE_STATS(free, -pages);

    return phys;
}

void *com_mm_pmm_alloc_zero(void) {
    return com_mm_pmm_alloc();
}

void *com_mm_pmm_alloc_many_zero(size_t pages) {
    return com_mm_pmm_alloc_many(pages);
}

void *com_mm_pmm_alloc_max(size_t *out_alloc_size, size_t pages) {
    if (1 == pages) {
        return com_mm_pmm_alloc();
    }

    FREELIST_LOCK(&MainFreeList);
    size_t                 alloc_size;
    struct freelist_entry *virt_ret = freelist_pop_front_nolock(&alloc_size,
                                                                &MainFreeList,
                                                                pages);
    FREELIST_UNLOCK(&MainFreeList);

    *virt_ret  = (struct freelist_entry){0};
    void *phys = (void *)ARCH_HHDM_TO_PHYS(virt_ret);

    struct page_meta model = {.num_ref = 1,
                              .state   = E_PAGE_STATE_TAKEN,
                              .type    = E_PAGE_TYPE_ANONYMOUS};
    page_meta_set(phys, &model, alloc_size);

    UPDATE_STATS(used, +alloc_size);
    UPDATE_STATS(free, -alloc_size);

    if (NULL != out_alloc_size) {
        *out_alloc_size = alloc_size;
    }

    return phys;
}

void *com_mm_pmm_alloc_max_zero(size_t *out_alloc_size, size_t pages) {
    return com_mm_pmm_alloc_max(out_alloc_size, pages);
}

void com_mm_pmm_hold(void *page) {
    struct page_meta *page_meta = page_meta_get(page);
    KASSERT(E_PAGE_STATE_FREE != page_meta->state);
    __atomic_add_fetch(&page_meta->num_ref, 1, __ATOMIC_SEQ_CST);
}

void com_mm_pmm_free(void *page) {
    struct page_meta *page_meta = page_meta_get(page);

    if (E_PAGE_STATE_TAKEN != page_meta->state ||
        0 != __atomic_add_fetch(&page_meta->num_ref, -1, __ATOMIC_SEQ_CST)) {
        return;
    }

    struct lazylist_entry *entry = (void *)ARCH_PHYS_TO_HHDM(page);
    entry->pages                 = 1;

    LAZYLIST_LOCK(&ZeroLazyList);
    lazylist_add_nolock(&ZeroLazyList, entry);
    LAZYLIST_UNLOCK(&ZeroLazyList);

    size_t to_zero_notify = READ_STATS(usable) / 10000 * CONFIG_PMM_NOTIFY_ZERO;
    if (UPDATE_STATS(to_zero, +1) >= to_zero_notify) {
        LAZYLIST_NOTIFY(&ZeroLazyList);
    }
}

void com_mm_pmm_free_many(void *base, size_t pages) {
    for (size_t i = 0; i < pages; i++) {
        com_mm_pmm_free((uint8_t *)base + i * ARCH_PAGE_SIZE);
    }
}

void com_mm_pmm_get_stats(com_pmm_stats_t *out) {
    *out = MemoryStats;
}

void com_mm_pmm_init_threads(void) {
    KLOG("initializing pmm threads");
    TAILQ_INIT(&ZeroLazyList.waitlist);
    TAILQ_INIT(&InsertLazyList.waitlist);
    TAILQ_INIT(&DefragThreadWaitlist);
    KHASHMAP_INIT(&InsertThreadDefragMap);

    // Zero thread
    com_thread_t *zero_thread = com_sys_thread_new_kernel(NULL,
                                                          pmm_zero_thread);
    zero_thread->runnable     = true;
    TAILQ_INSERT_TAIL(&ARCH_CPU_GET()->sched_queue, zero_thread, threads);

    // Insert thread
    com_thread_t *insert_thread = com_sys_thread_new_kernel(NULL,
                                                            pmm_insert_thread);
    insert_thread->runnable     = true;
    TAILQ_INSERT_TAIL(&ARCH_CPU_GET()->sched_queue, insert_thread, threads);

    // Defrag thread
}

void com_mm_pmm_init(void) {
    KLOG("initializing pmm");
    arch_memmap_t *memmap       = arch_info_get_memmap();
    uintptr_t      highest_addr = 0;
    TAILQ_INIT(&MainFreeList.head);
    KSEARCHTREE_INIT(&MainFreeList.tree);

    // Calculate memory map & page meta array size
    for (uintmax_t i = 0; i < memmap->entry_count; i++) {
        arch_memmap_entry_t *entry = memmap->entries[i];
        MemoryStats.total += entry->length;
        uintptr_t seg_top = entry->base + entry->length;

        KDEBUG("segment: base=%p top=%p usable=%u length=%u",
               entry->base,
               seg_top,
               ARCH_MEMMAP_IS_USABLE(entry),
               entry->length);

        if (seg_top > highest_addr) {
            highest_addr = seg_top;
        }

        if (!ARCH_MEMMAP_IS_USABLE(entry)) {
            MemoryStats.reserved += entry->length;
            continue;
        }

        kmemset((void *)ARCH_PHYS_TO_HHDM(entry->base), entry->length, 0);
        MemoryStats.usable += entry->length;
    }

    MemoryStats.total = highest_addr;
    KDEBUG("searched all segments, found highest address at %p", highest_addr);

    // Compute the actual size of the page meta array
    size_t page_meta_size  = PAGE_META_ARRAY_SIZE(highest_addr);
    size_t page_meta_pages = (page_meta_size + ARCH_PAGE_SIZE - 1) /
                             ARCH_PAGE_SIZE;
    size_t page_meta_aligned_size = page_meta_pages * ARCH_PAGE_SIZE;
    KDEBUG("page meta array size = %u byte(s) = %u page(s) = %u aligned "
           "byte(s)",
           page_meta_size,
           page_meta_pages,
           page_meta_aligned_size);

    // Find a segment that fits the array and allocate it there
    for (uintmax_t i = 0; i < memmap->entry_count; i++) {
        arch_memmap_entry_t *entry = memmap->entries[i];

        if (!ARCH_MEMMAP_IS_USABLE(entry)) {
            continue;
        }

        if (NULL == PageMeta.buffer &&
            entry->length >= page_meta_aligned_size) {
            PageMeta.buffer = (void *)ARCH_PHYS_TO_HHDM(entry->base);
            PageMeta.len    = PAGE_META_ARRAY_LEN(highest_addr);
            // Already zeroed before. Remember, 0 == E_PAGE_STATE_FREE

            // We now reserve the pages used for this array
            struct page_meta model = {.num_ref = 1,
                                      .state   = E_PAGE_STATE_RESERVED};
            page_meta_set((void *)entry->base, &model, page_meta_pages);
            MemoryStats.usable -= page_meta_aligned_size;
            MemoryStats.reserved += page_meta_aligned_size;

            // We also add this to the freelist. This list entry of couse is
            // a little special since a part of it has been reserved. The
            // address is rounded down to the nearest fully free page
            // boundary to guarantee that allocations will always return
            // aligned addresses. This is only performed if there is still
            // at least one page elft
            if (entry->length >= page_meta_aligned_size + ARCH_PAGE_SIZE) {
                size_t remaining_length = entry->length -
                                          page_meta_aligned_size;
                size_t remaining_pages = remaining_length / ARCH_PAGE_SIZE;

                struct freelist_entry
                    *list_entry   = (void *)(ARCH_PHYS_TO_HHDM(entry->base) +
                                           page_meta_aligned_size);
                list_entry->pages = remaining_pages;
                freelist_add_ordered_nolock(&MainFreeList, list_entry);
            }

            KDEBUG("page meta array: base=%p, segment=%u, length=%u",
                   PageMeta.buffer,
                   i,
                   PageMeta.len);
            continue;
        }

        // If we got here, it means the entire entry is free for us!
        struct freelist_entry *list_entry = (void *)(ARCH_PHYS_TO_HHDM(
            entry->base));
        list_entry->pages                 = entry->length / ARCH_PAGE_SIZE;
        freelist_add_ordered_nolock(&MainFreeList, list_entry);
    }

    KDEBUG("reserving remaining unusable pages");

    // We couldn't reserve all unusable regions before because the array
    // might not have been set yet. But we can not
    for (uintmax_t i = 0; i < memmap->entry_count; i++) {
        arch_memmap_entry_t *entry = memmap->entries[i];

        if (!ARCH_MEMMAP_IS_USABLE(entry)) {
            struct page_meta model = {.num_ref = 1,
                                      .state   = E_PAGE_STATE_RESERVED};
            page_meta_set((void *)entry->base,
                          &model,
                          entry->length / ARCH_PAGE_SIZE);
        }
    }

    MemoryStats.free = MemoryStats.usable;

    struct freelist_entry *e, *_;
    TAILQ_FOREACH_SAFE(e, &MainFreeList.head, tqe, _) {
        KDEBUG("freelist entry %p has %u pages", e, e->pages);
    }
}
