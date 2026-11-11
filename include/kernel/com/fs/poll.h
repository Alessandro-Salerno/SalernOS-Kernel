#pragma once

#include <kernel/com/fs/file.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <vendor/tailq.h>

typedef struct Poller {
    Waitlist waiters;
    Spinlock lock;
    bool notified;
} Poller;

typedef struct Polled {
    Poller *poller;
    File *file;
    LIST_ENTRY(Polled) polled_list;
} Polled;

typedef struct PollHead {
    LIST_HEAD(, Polled) polled_list;
    Spinlock lock;
} PollHead;

static inline void poll_notify(Poller *poller) {
    spinlock_acq(&poller->lock);
    poller->notified = true;
    sched_notify_all(&poller->waiters);
    spinlock_rel(&poller->lock);
}
