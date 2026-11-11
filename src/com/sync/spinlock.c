#include <arch/cpu.h>
#include <kernel/com/io/log.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/panic.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>

#define INCREMENT_LOCK_DEPTH(thr) \
    if (thr != NULL) thr->lock_depth++;
#define DECREMENT_LOCK_DEPTH(thr) \
    if (thr != NULL) thr->lock_depth--;

#define INCREMENT_CURR_LOCK_DEPTH() INCREMENT_LOCK_DEPTH(cpu_get_thread())
#define DECREMENT_CURR_LOCK_DEPTH() DECREMENT_LOCK_DEPTH(cpu_get_thread())

#if CONFIG_SPINLOCK_DEBUG

#define LOCK_VALUE(lock) (lock)->lock

#define LOCK_ERROR(fmt, ...)                                        \
    panic(NULL, "(lock error) " fmt " [called at %p]", __VA_ARGS__, \
          __builtin_return_address(0));

#else

#define LOCK_VALUE(lock) *(lock)
#define LOCK_ERROR(fmt, ...)

#endif

static inline void decrement_lock_depth_tested(void) {
    Thread *curr_thread = cpu_get_thread();

    if (likely(curr_thread != NULL)) {
        int old_depth = curr_thread->lock_depth--;

        if (unlikely(old_depth < 1)) {
            LOCK_ERROR("trying to unlock lock on thread (tid = %d) with "
                       "invalid lock depth %d",
                       curr_thread->tid, old_depth);
        }

        if (old_depth == 1) cpu_enable_interrupts();
    }
}

void spinlock_acq(Spinlock *lock) {
    if (cpu_get_thread() == NULL) return;

    ProfilerData profiler_data = profiler_start_function(
        PROFILE_FUNC_SPINLOCK_ACQ);
    cpu_disable_interrupts();
    INCREMENT_CURR_LOCK_DEPTH();

    u32 backoff = 1;
    while (true) {
        // this is before the spinning since hopefully the lock is uncontended
        if (__atomic_exchange_n(&LOCK_VALUE(lock), SPINLOCK_HELD_VALUE,
                                __ATOMIC_ACQUIRE) == SPINLOCK_FREE_VALUE) {
            break;
        }
        // spin with no ordering constraints
        while (__atomic_load_n(&LOCK_VALUE(lock), __ATOMIC_RELAXED) ==
               SPINLOCK_HELD_VALUE) {
            for (u32 i = 0; i < backoff; i++) cpu_pause();
            if (backoff < SPINLOCK_BACKOFF_MAX) backoff *= 2;
        }
    }

#if CONFIG_SPINLOCK_DEBUG
    lock->holder_thread = cpu_get_thread();
    lock->last_acq_ip = (void *)__builtin_return_address(0);
    lock->last_rel_ip = NULL;
#endif
    profiler_end_function(&profiler_data);
}

bool spinlock_acq_timeout(Spinlock *lock, u64 timeout_ns) {
    cpu_disable_interrupts();
    INCREMENT_CURR_LOCK_DEPTH();

    u64 start_ns = cpu_get_time();
    u64 end_ns = start_ns + timeout_ns;

    while (true) {
        // this is before the spinning since hopefully the lock is uncontended
        if (__atomic_exchange_n(&LOCK_VALUE(lock), SPINLOCK_HELD_VALUE,
                                __ATOMIC_ACQUIRE) == SPINLOCK_FREE_VALUE) {
            break;
        }
        // spin with no ordering constraints
        while (__atomic_load_n(&LOCK_VALUE(lock), __ATOMIC_RELAXED) ==
               SPINLOCK_HELD_VALUE) {
            if (end_ns <= cpu_get_time()) goto fail;
            cpu_pause();
        }
    }

#if CONFIG_SPINLOCK_DEBUG
    lock->holder_thread = cpu_get_thread();
    lock->last_acq_ip = (void *)__builtin_return_address(0);
    lock->last_rel_ip = NULL;
#endif
    return true;

fail:
    // Control reaches here only in case of timeout
    DECREMENT_CURR_LOCK_DEPTH();
    return false;
}

void spinlock_rel(Spinlock *lock) {
    if (unlikely(LOCK_VALUE(lock) == SPINLOCK_FREE_VALUE)) {
        LOCK_ERROR("trying to unlock unlocked lock %p", lock);
    }

    if (unlikely(LOCK_VALUE(lock) != SPINLOCK_HELD_VALUE)) {
        LOCK_ERROR("lock at %p has impossible value %d", lock, lock->lock);
    }

    if (cpu_get_thread() == NULL) return;

#if CONFIG_SPINLOCK_DEBUG
    lock->last_rel_ip = (void *)__builtin_return_address(0);
    lock->holder_thread = NULL;
    lock->last_acq_ip = NULL;
#endif
    __atomic_store_n(&LOCK_VALUE(lock), SPINLOCK_FREE_VALUE, __ATOMIC_RELEASE);
    decrement_lock_depth_tested();
}

void spinlock_fake_acq(void) {
    cpu_disable_interrupts();
    INCREMENT_CURR_LOCK_DEPTH();
}

void spinlock_fake_rel(void) { decrement_lock_depth_tested(); }

static ContextMessage spinlock_wait_rel(void *primitive) {
    spinlock_rel(primitive);
    return (ContextMessage){0};
}

static void spinlock_wait_acq(void *primitive) { spinlock_acq(primitive); }

int spinlock_wait(Spinlock *lock, struct Waitlist *waitlist, u64 timeout,
                  bool interruptible) {
    static const SchedWaitOps ops = {.release = spinlock_wait_rel,
                                     .acquire = spinlock_wait_acq};
    return sched_wait(waitlist, timeout, interruptible, &ops, lock);
}
