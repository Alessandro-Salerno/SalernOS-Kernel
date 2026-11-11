#include <kernel/com/sync/sync.h>
#include <kernel/com/sys/sched.h>

void sync_acq(Sync *sync) {
    if (sync->flags & SYNC_FLAGS_MUTEX) mutex_acq(&sync->lock.mutex);
    else spinlock_acq(&sync->lock.spinlock);
}

void sync_rel(Sync *sync) {
    if (sync->flags & SYNC_FLAGS_MUTEX) mutex_rel(&sync->lock.mutex);
    else spinlock_rel(&sync->lock.spinlock);
}

void sync_wait(Sync *sync, Waitlist *waitlist) {
    if (sync->flags & SYNC_FLAGS_MUTEX) {
        mutex_wait(&sync->lock.mutex, waitlist, 0, true);
    } else {
        spinlock_wait(&sync->lock.spinlock, waitlist, 0, true);
    }
}

int sync_wait_timeout(Sync *sync, struct Waitlist *waitlist, u64 timeout) {
    if (sync->flags & SYNC_FLAGS_MUTEX) {
        return mutex_wait(&sync->lock.mutex, waitlist, timeout, true);
    }
    return spinlock_wait(&sync->lock.spinlock, waitlist, timeout, true);
}

void sync_notify(Sync *sync, Waitlist *waitlist) {
    (void)sync;
    sched_notify(waitlist);
}

void sync_notify_all(Sync *sync, Waitlist *waitlist) {
    (void)sync;
    sched_notify_all(waitlist);
}

void sync_wait_uninterruptible(Sync *sync, Waitlist *waitlist) {
    if (sync->flags & SYNC_FLAGS_MUTEX) {
        mutex_wait(&sync->lock.mutex, waitlist, 0, false);
    } else {
        spinlock_wait(&sync->lock.spinlock, waitlist, 0, false);
    }
}

void sync_init_mutex(Sync *sync) {
    sync->flags = SYNC_FLAGS_MUTEX;
    mutex_init(&sync->lock.mutex);
}

void sync_init_spinlock(Sync *sync) {
    sync->flags = 0;
    sync->lock.spinlock = SPINLOCK_NEW();
}
