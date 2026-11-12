#pragma once

#include <kernel/com/sync/waitlist.h>

#define SEMAPHORE_WAIT_POLL  0
#define SEMAPHORE_WAIT_INDEF UINT64_MAX

typedef struct Semaphore {
    Spinlock lock;
    Waitlist waiters;
    usize count;
} Semaphore;

void semaphore_init(Semaphore *sem, usize count);
bool semaphore_acq(Semaphore *sem, u64 timeout);
void semaphore_rel(Semaphore *sem);
void semaphore_reset(Semaphore *sem);
