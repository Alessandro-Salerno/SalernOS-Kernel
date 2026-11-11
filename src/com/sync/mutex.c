#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/sync/mutex.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>

static bool try_acq_internal(Mutex *mutex, usize retries) {
    for (usize i = 0; i < retries; i++) {
        spinlock_acq(&mutex->lock);
        if (!mutex->locked) return true;

        if (__atomic_load_n(&mutex->owner->state, __ATOMIC_RELAXED) !=
            THREAD_STATE_RUNNING) {
            spinlock_rel(&mutex->lock);
            return false;
        }

        spinlock_rel(&mutex->lock);
        cpu_pause();
    }

    return false;
}

bool mutex_try_acq(Mutex *mutex) {
    if (!try_acq_internal(mutex, 1)) return false;

    mutex->locked = true;
    mutex->owner = cpu_get_thread();
    spinlock_rel(&mutex->lock);
    return true;
}

void mutex_acq(Mutex *mutex) {
    if (cpu_get_thread() == NULL) return;

    ProfilerData profiler_data = profiler_start_function(
        PROFILE_FUNC_MUTEX_ACQ);
    mutex_acq_timeout(mutex, 0);
    profiler_end_function(&profiler_data);
}

bool mutex_acq_timeout(Mutex *mutex, u64 timeout) {
    Thread *curr_thread = cpu_get_thread();
    if (curr_thread == NULL) return true;
    assert(curr_thread == NULL || curr_thread->lock_depth == 0);

    if (try_acq_internal(mutex, CONFIG_MUTEX_RETRIES)) goto take;

    spinlock_acq(&mutex->lock);
    while (mutex->locked) {
        if (spinlock_wait(&mutex->lock, &mutex->waiters, timeout, false) ==
            ETIMEDOUT) {
            assert(timeout != 0);
            spinlock_rel(&mutex->lock);
            sched_notify(&mutex->waiters);
            return false;
        }
    }

take:
    mutex->locked = true;
    mutex->owner = curr_thread;
    spinlock_rel(&mutex->lock);
    return true;
}

void mutex_rel(Mutex *mutex) {
    Thread *curr_thread = cpu_get_thread();
    if (curr_thread == NULL) return;

    // assert(NULL == curr_thread || 0 == curr_thread->lock_depth);

    spinlock_acq(&mutex->lock);
    assert(mutex->locked);
    mutex->locked = false;
    mutex->owner = NULL;
    sched_notify(&mutex->waiters);
    spinlock_rel(&mutex->lock);
    // sched_yield();
}

static void mutex_wait_prepare(void *primitive) {
    Mutex *mutex = primitive;
    spinlock_acq(&mutex->lock);
}

static void mutex_post_switch(void *arg, uptr epoch, bool success) {
    Thread *thread = arg;
    if (thread == NULL) return;
    if (success) {
        thread_lock(thread);
        if (epoch == thread->epoch) {
            assert(thread->waiting_on != NULL);
            sched_notify_thread_nolock(thread);
        }
        thread_unlock(thread);
    }
    thread_rel(thread);
}

static ContextMessage mutex_wait_rel(void *primitive) {
    Mutex *mutex = primitive;
    ContextMessage message = {.callback = mutex_post_switch};
    assert(mutex->locked && mutex->owner == cpu_get_thread());
    mutex->locked = false;
    mutex->owner = NULL;
    spinlock_acq(&mutex->waiters.lock);
    Thread *thread = TAILQ_FIRST(&mutex->waiters.queue);
    if (thread != NULL) {
        thread_hold(thread);
        message.arg = thread;
        message.value = __atomic_load_n(&thread->epoch, __ATOMIC_RELAXED);
    }
    spinlock_rel(&mutex->waiters.lock);
    spinlock_rel(&mutex->lock);
    return message;
}

static void mutex_wait_acq(void *primitive) { mutex_acq(primitive); }

int mutex_wait(Mutex *mutex, Waitlist *waitlist, u64 timeout,
               bool interruptible) {
    static const SchedWaitOps ops = {.prepare = mutex_wait_prepare,
                                     .release = mutex_wait_rel,
                                     .acquire = mutex_wait_acq};
    return sched_wait(waitlist, timeout, interruptible, &ops, mutex);
}

void mutex_init(Mutex *mutex) {
    mutex->lock = SPINLOCK_NEW();
    mutex->owner = NULL;
    mutex->locked = false;
    waitlist_init(&mutex->waiters);
}
