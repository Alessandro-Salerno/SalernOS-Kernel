#include <kernel/com/sync/rwlock.h>
#include <kernel/com/sys/sched.h>

void rwlock_acq_read(RWLock *rwlock) {
    spinlock_acq(&rwlock->lock);

    while (rwlock->writer_active || rwlock->waiting_writers > 0) {
        spinlock_wait(&rwlock->lock, &rwlock->readers_waitlist, 0, false);
    }

    rwlock->active_readers++;
    spinlock_rel(&rwlock->lock);
}

void rwlock_rel_read(RWLock *rwlock) {
    spinlock_acq(&rwlock->lock);

    rwlock->active_readers--;
    if (rwlock->active_readers == 0 && rwlock->waiting_writers > 0) {
        sched_notify(&rwlock->writers_waitlist);
    }

    spinlock_rel(&rwlock->lock);
}

void rwlock_acq_write(RWLock *rwlock) {
    spinlock_acq(&rwlock->lock);
    rwlock->waiting_writers++;

    while (rwlock->writer_active || rwlock->active_readers > 0) {
        spinlock_wait(&rwlock->lock, &rwlock->writers_waitlist, 0, false);
    }

    assert(!rwlock->writer_active);
    rwlock->waiting_writers--;
    rwlock->writer_active = true;
    spinlock_rel(&rwlock->lock);
}

void rwlock_rel_write(RWLock *rwlock) {
    spinlock_acq(&rwlock->lock);
    rwlock->writer_active = false;

    if (rwlock->waiting_writers == 0) {
        sched_notify_all(&rwlock->readers_waitlist);
    } else {
        sched_notify(&rwlock->writers_waitlist);
    }

    spinlock_rel(&rwlock->lock);
}

void rwlock_init(RWLock *rwlock) {
    rwlock->lock = SPINLOCK_NEW();
    rwlock->active_readers = 0;
    rwlock->waiting_writers = 0;
    rwlock->writer_active = false;
    waitlist_init(&rwlock->readers_waitlist);
    waitlist_init(&rwlock->writers_waitlist);
}
