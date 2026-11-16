#include <arch/info.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/failnth.h>
#include <kernel/com/util.h>
#include <vendor/tailq.h>

#define NUM_SLABS (PAGE_SIZE / 16)

// Every slab page starts with this header, followed by its entries
typedef struct SlabPage {
    uptr next;
    usize num_used;
    LIST_ENTRY(SlabPage) link;
} SlabPage;

_Static_assert(sizeof(SlabPage) == PAGE_SIZE - SLAB_MAX_SIZE,
               "SLAB_MAX_SIZE must match the slab page header");

// Pages with at least one free entry. At most one of them is empty
typedef struct Slab {
    Spinlock lock;
    LIST_HEAD(, SlabPage) partial;
} Slab;

static Slab g_slabs[NUM_SLABS] = {
    [0 ... NUM_SLABS - 1] = {.lock = SPINLOCK_NEW()}};
#if CONFIG_FAIL_NTH
static usize g_num_objects = 0;
#endif

static SlabPage *new_page(usize entry_size) {
    void *new_page = pmm_allocz();
    if (new_page == NULL) return NULL;
    SlabPage *page = (void *)PHYS_TO_HHDM(new_page);
    usize max = SLAB_MAX_SIZE / entry_size - 1;
    uptr *list_arr = (uptr *)(page + 1);
    usize off = entry_size / sizeof(uptr);

    for (usize i = 0; i < max; i++) {
        list_arr[i * off] = (uptr)&list_arr[(i + 1) * off];
    }

    list_arr[max * off] = (uptr)NULL;
    page->next = (uptr)list_arr;
    return page;
}

void *slab_alloc(usize size) {
    assert(size <= SLAB_MAX_SIZE);
    if (failnth_should_fail()) return NULL;
    usize i = (size + 15) / 16;
    Slab *s = &g_slabs[i];

    spinlock_acq(&s->lock);
    SlabPage *page = LIST_FIRST(&s->partial);
    if (page == NULL) {
        page = new_page(i * 16);
        if (page == NULL) {
            spinlock_rel(&s->lock);
            return NULL;
        }
        LIST_INSERT_HEAD(&s->partial, page, link);
    }

    uptr *old_next = (uptr *)page->next;
    page->next = *old_next;
    page->num_used++;
    if (page->next == 0) LIST_REMOVE(page, link);
    spinlock_rel(&s->lock);
#if CONFIG_FAIL_NTH
    __atomic_add_fetch(&g_num_objects, 1, __ATOMIC_RELAXED);
#endif

    *old_next = 0;
    return old_next;
}

void slab_free(void *ptr, usize size) {
    if (ptr == NULL) return;

    assert(size <= SLAB_MAX_SIZE);
    usize i = (size + 15) / 16;
    Slab *s = &g_slabs[i];
    SlabPage *page = (void *)((uptr)ptr & ~(uptr)(PAGE_SIZE - 1));

    uptr *new_head = ptr;
    memset(new_head, 0, size);
    spinlock_acq(&s->lock);
    if (page->next == 0) LIST_INSERT_HEAD(&s->partial, page, link);
    *new_head = page->next;
    page->next = (uptr)new_head;
    page->num_used--;

    // Keep the last page with free entries, so that a class that keeps
    // allocating and freeing one entry does not keep allocating pages
    bool release = page->num_used == 0 &&
        (LIST_FIRST(&s->partial) != page || LIST_NEXT(page, link) != NULL);
    if (release) LIST_REMOVE(page, link);
    spinlock_rel(&s->lock);
#if CONFIG_FAIL_NTH
    __atomic_sub_fetch(&g_num_objects, 1, __ATOMIC_RELAXED);
#endif

    if (release) pmm_free((void *)HHDM_TO_PHYS(page));
}

#if CONFIG_FAIL_NTH
usize slab_get_num_objects(void) {
    return __atomic_load_n(&g_num_objects, __ATOMIC_RELAXED);
}
#endif
