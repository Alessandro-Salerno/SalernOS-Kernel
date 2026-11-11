#pragma once

#include <kernel/com/sync/mutex.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sync/waitlist.h>

#define SYNC_FLAGS_MUTEX 1

#define SYNC_NEW_SPINLOCK() (Sync){.lock.spinlock = SPINLOCK_NEW(), .flags = 0}

typedef struct Sync {
    union {
        Mutex mutex;
        Spinlock spinlock;
    } lock;
    int flags;
} Sync;

void sync_acq(Sync *sync);
void sync_rel(Sync *sync);
void sync_wait(Sync *sync, struct Waitlist *waitlist);
int sync_wait_timeout(Sync *sync, struct Waitlist *waitlist, u64 timeout);
void sync_notify(Sync *sync, struct Waitlist *waiters);
void sync_notify_all(Sync *sync, struct Waitlist *waiters);

void sync_wait_uninterruptible(Sync *sync, Waitlist *waitlist);

void sync_init_mutex(Sync *sync);
void sync_init_spinlock(Sync *sync);
