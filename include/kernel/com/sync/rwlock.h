#pragma once

#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sync/waitlist.h>
#include <kernel/com/util.h>

typedef struct RWLock {
    Spinlock lock;
    usize active_readers;
    usize waiting_writers;
    bool writer_active;

    Waitlist readers_waitlist;
    Waitlist writers_waitlist;
} RWLock;

void rwlock_acq_read(RWLock *rwlock);
void rwlock_rel_read(RWLock *rwlock);
void rwlock_acq_write(RWLock *rwlock);
void rwlock_rel_write(RWLock *rwlock);

void rwlock_init(RWLock *rwlock);
