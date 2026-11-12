#include <arch/cpu.h>
#include <kernel/com/sync/semaphore.h>
#include <kernel/com/sys/sched.h>

void semaphore_init(Semaphore *sem, usize count) {
    sem->lock = SPINLOCK_NEW();
    waitlist_init(&sem->waiters);
    sem->count = count;
}

bool semaphore_acq(Semaphore *sem, u64 timeout) {
    spinlock_acq(&sem->lock);
    u64 start = sem->count == 0 && timeout != 0 && timeout != UINT64_MAX
        ? cpu_get_time()
        : 0;

    while (sem->count == 0) {
        u64 remaining = 0;
        if (timeout != UINT64_MAX) {
            u64 elapsed = cpu_get_time() - start;
            if (timeout == 0 || elapsed >= timeout) {
                spinlock_rel(&sem->lock);
                return false;
            }
            remaining = timeout - elapsed;
        }

        // This burns CPU like crazy but I doubt there's a better solution when
        // some components want to wait in bootstrap context
        if (cpu_get_thread() == NULL) {
            spinlock_rel(&sem->lock);
            cpu_pause();
            spinlock_acq(&sem->lock);
        } else spinlock_wait(&sem->lock, &sem->waiters, remaining, false);
    }

    sem->count--;
    spinlock_rel(&sem->lock);
    return true;
}

void semaphore_rel(Semaphore *sem) {
    spinlock_acq(&sem->lock);
    if (sem->count != SIZE_MAX) sem->count++;
    sched_notify(&sem->waiters);
    spinlock_rel(&sem->lock);
}

void semaphore_reset(Semaphore *sem) {
    spinlock_acq(&sem->lock);
    sem->count = 0;
    spinlock_rel(&sem->lock);
}
