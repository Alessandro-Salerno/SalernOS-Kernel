#pragma once

#include <kernel/com/util.h>

#define SPINLOCK_BACKOFF_MAX 64

#if CONFIG_SPINLOCK_DEBUG

#define SPINLOCK_HELD_VALUE 144
#define SPINLOCK_FREE_VALUE 0

#define SPINLOCK_NEW() \
    (Spinlock){.lock = SPINLOCK_FREE_VALUE, .holder_thread = (void *)0xAAAAAAA}

typedef struct Spinlock {
    int lock;
    void *holder_thread;
    void *last_acq_ip;
    void *last_rel_ip;
} Spinlock;

static inline bool spinlock_is_held(const Spinlock *lock) {
    return lock->lock == SPINLOCK_HELD_VALUE;
}

#else

#define SPINLOCK_HELD_VALUE 1
#define SPINLOCK_FREE_VALUE 0

#define SPINLOCK_NEW() (Spinlock) SPINLOCK_FREE_VALUE

typedef int Spinlock;

static inline bool spinlock_is_held(const Spinlock *lock) {
    return *lock == SPINLOCK_HELD_VALUE;
}

#endif

void spinlock_acq(Spinlock *lock);
bool spinlock_acq_timeout(Spinlock *lock, u64 timeout_ns);
void spinlock_rel(Spinlock *lock);
void spinlock_fake_acq(void);
void spinlock_fake_rel(void);

struct Waitlist;
// The lock is held both on entry and on every return
int spinlock_wait(Spinlock *lock, struct Waitlist *waitlist, u64 timeout,
                  bool interruptible);
