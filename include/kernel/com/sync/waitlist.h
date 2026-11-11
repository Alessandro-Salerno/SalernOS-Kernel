#pragma once

#include <kernel/com/sync/spinlock.h>
#include <kernel/com/util.h>
#include <vendor/tailq.h>

struct Thread;
TAILQ_HEAD(ThreadTailQ, Thread);

typedef struct Waitlist {
    struct ThreadTailQ queue;
    Spinlock lock;
    usize count;
} Waitlist;

static inline void waitlist_init(Waitlist *waitlist) {
    TAILQ_INIT(&waitlist->queue);
    waitlist->count = 0;
    waitlist->lock = SPINLOCK_NEW();
}

static inline bool waitlist_empty(const Waitlist *waitlist) {
    return TAILQ_EMPTY(&waitlist->queue);
}
