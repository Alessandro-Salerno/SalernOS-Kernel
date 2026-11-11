#include <arch/info.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/util.h>

#define NUM_SLABS (PAGE_SIZE / 16)

typedef struct {
    uptr next;
} Slab;

static Slab g_slabs[NUM_SLABS] = {0};
static Spinlock g_lock = SPINLOCK_NEW();

static void init(Slab *s, usize entry_size) {
    entry_size = (entry_size + 15) & ~15UL;
    void *new_page = pmm_allocz();
    s->next = PHYS_TO_HHDM(new_page);
    usize max = PAGE_SIZE / entry_size - 1;
    uptr *list_arr = (uptr *)s->next;
    usize off = entry_size / sizeof(uptr);

    for (usize i = 0; i < max; i++) {
        list_arr[i * off] = (uptr)&list_arr[(i + 1) * off];
    }

    list_arr[max * off] = (uptr)NULL;
}

void *slab_alloc(usize size) {
    usize i = (size + 15) / 16;
    assert(i < NUM_SLABS);
    Slab *s = &g_slabs[i];

    spinlock_acq(&g_lock);
    if (s->next == 0) init(s, size);
    uptr *old_next = (uptr *)s->next;
    s->next = *old_next;
    spinlock_rel(&g_lock);

    *old_next = 0;
    return old_next;
}

void slab_free(void *ptr, usize size) {
    if (ptr == NULL) return;

    usize i = (size + 15) / 16;
    assert(i < NUM_SLABS);
    Slab *s = &g_slabs[i];

    uptr *new_head = ptr;
    memset(new_head, 0, size);
    spinlock_acq(&g_lock);
    *new_head = s->next;
    s->next = (uptr)new_head;
    spinlock_rel(&g_lock);

    // TODO: free slab page if needed
}
