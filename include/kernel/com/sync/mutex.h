#pragma once

#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sync/waitlist.h>
#include <kernel/com/util.h>
#include <vendor/tailq.h>

typedef struct Mutex {
    Spinlock lock;
    bool locked;
    struct Thread *owner;
    Waitlist waiters;
} Mutex;

bool mutex_try_acq(Mutex *mutex);
void mutex_acq(Mutex *mutex);
bool mutex_acq_timeout(Mutex *mutex, u64 timeout);
void mutex_rel(Mutex *mutex);

// Wait on a condition while atomically releasing/reacquiring this mutex
int mutex_wait(Mutex *mutex, Waitlist *waitlist, u64 timeout,
               bool interruptible);

void mutex_init(Mutex *mutex);
