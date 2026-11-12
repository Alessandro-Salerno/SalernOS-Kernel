#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/platform/context.h>
#include <kernel/platform/x86-64/smp.h>
#include <stdatomic.h>
#include <sys/uio.h>
#include <vendor/tailq.h>

_Static_assert(CONFIG_IOV_MAX * sizeof(struct iovec) <= PAGE_SIZE,
               "the scratch page must hold CONFIG_IOV_MAX iovecs");

static pid_t g_next_tid = 0;

// Cpu locks outlive every thread. Recheck after acquiring a possibly stale
// lock
void thread_lock(Thread *thread) {
    for (;;) {
        Spinlock *lock = __atomic_load_n(&thread->sched_lock, __ATOMIC_ACQUIRE);
        spinlock_acq(lock);
        if (lock == __atomic_load_n(&thread->sched_lock, __ATOMIC_ACQUIRE)) {
            return;
        }
        spinlock_rel(lock);
    }
}

void thread_unlock(Thread *thread) {
    spinlock_rel(__atomic_load_n(&thread->sched_lock, __ATOMIC_RELAXED));
}

void thread_hold(Thread *thread) {
    __atomic_add_fetch(&thread->refs, 1, __ATOMIC_RELAXED);
}

void thread_rel(Thread *thread) {
    if (__atomic_sub_fetch(&thread->refs, 1, __ATOMIC_ACQ_REL) == 0) {
        assert(__atomic_load_n(&thread->state, __ATOMIC_RELAXED) ==
               THREAD_STATE_EXITED);
        assert(thread->cpu == NULL && thread->waiting_on == NULL);
        // Reclamation can sleep: never free from a switch-in or idle context
        spinlock_fake_acq();
        SLIST_INSERT_HEAD(&cpu_get()->zombies, thread, zombies);
        spinlock_fake_rel();
    }
}

void thread_reap(void) {
    Thread *curr = cpu_get_thread();
    if (curr == NULL || curr == cpu_get()->idle_thread || curr->lock_depth) {
        return;
    }
    spinlock_fake_acq();
    struct ThreadSList zombies = cpu_get()->zombies;
    SLIST_INIT(&cpu_get()->zombies);
    spinlock_fake_rel();
    while (!SLIST_EMPTY(&zombies)) {
        Thread *list = SLIST_FIRST(&zombies);
        SLIST_REMOVE_HEAD(&zombies, zombies);
        if (list->proc != NULL) proc_rel(list->proc);
        pmm_free((void *)HHDM_TO_PHYS(list->kernel_stack) - PAGE_SIZE);
        callout_destroy(list->timed_wait_callout);
        pmm_free((void *)HHDM_TO_PHYS(list->scratch));
        pmm_free((void *)HHDM_TO_PHYS(list));
    }
}

static Thread *new_thread(Proc *proc, Context ctx) {
    Thread *thread = (void *)PHYS_TO_HHDM(pmm_alloc());
    thread->proc = proc;
    thread->ctx = ctx;
    thread->lock_depth = 1;
    thread->last_cpu = cpu_get();
    __atomic_store_n(&thread->sched_lock, &thread->last_cpu->runqueue_lock,
                     __ATOMIC_RELEASE);
    // Scheduler ownership lasts until thread_exit_complete, after switch-out
    thread->refs = 1;
    thread->epoch = 0;
    thread->wait_interruptible = false;
    thread->exit_requested = false;
    thread->signal_stopped = false;
    thread->cpu = NULL;
    __atomic_store_n(&thread->state, THREAD_STATE_NEW, __ATOMIC_RELAXED);
    thread->kernel_stack = (void *)PHYS_TO_HHDM(pmm_alloc()) + PAGE_SIZE;
    CONTEXT_INIT_EXTRA(thread->xctx);
    thread->tid = __atomic_fetch_add(&g_next_tid, 1, __ATOMIC_RELAXED);

    thread->usercopy_ctx = NULL;
    thread->scratch = (void *)PHYS_TO_HHDM(pmm_alloc());
    thread->waiting_on = NULL;
    thread->ctime = cpu_get_timestamp();
    thread->timed_wait_callout = callout_new(NULL, thread, 0);
    thread->timed_wait_callout->permanent = true;
    thread->timed_wait_callout->snapshot = true;

    thread->real_timer.lock = SPINLOCK_NEW();

    signal_sigmask_init(&thread->pending_signals);
    signal_sigmask_init(&thread->masked_signals);

    if (proc != NULL) proc_add_thread(proc, thread);

    return thread;
}

Thread *thread_new(Proc *proc, void *stack, u64 stack_size, void *entry) {
    Context ctx = {0};
    CONTEXT_THREAD_SET(ctx, stack, stack_size, entry);
    return new_thread(proc, ctx);
}

Thread *thread_new_kernel(Proc *proc, void *entry, void *arg) {
    Context ctx = {0};
    Thread *t = new_thread(proc, ctx);
    CONTEXT_THREAD_SET_KERNEL(t->ctx, t->kernel_stack, entry, arg);
    return t;
}

void thread_transition(Thread *thread, ThreadState state) {
    thread_lock(thread);
    thread_transition_nolock(thread, state);
    thread_unlock(thread);
}

void thread_transition_nolock(Thread *thread, ThreadState state) {
    // debug checks
    assert(__atomic_load_n(&thread->state, __ATOMIC_RELAXED) !=
           THREAD_STATE_EXITED);
    switch (state) {
        case THREAD_STATE_NEW:
            assert(!"cannot transition to new state from any state");
            break;
        case THREAD_STATE_READY:
            assert(__atomic_load_n(&thread->state, __ATOMIC_RELAXED) !=
                   THREAD_STATE_EXITED);
            assert(thread->waiting_on == NULL);
            break;
        case THREAD_STATE_WAITING:
            assert(thread->waiting_on != NULL);
            break;
        case THREAD_STATE_RUNNING:
            assert(thread->cpu != NULL);
            assert(thread->waiting_on == NULL);
            break;
        case THREAD_STATE_EXITED:
            break;
        default:
            assert(!"invalid argument");
            break;
    }

    __atomic_store_n(&thread->state, state, __ATOMIC_RELAXED);
    __atomic_add_fetch(&thread->epoch, 1, __ATOMIC_RELAXED);
}

void thread_exit(Thread *thread) {
    thread_lock(thread);
    thread_exit_nolock(thread);
}

void thread_request_exit(Thread *thread) {
    thread_lock(thread);
    __atomic_store_n(&thread->exit_requested, true, __ATOMIC_RELEASE);
    sched_interrupt_nolock(thread);
    thread_unlock(thread);
}

static void thread_exit_complete(void *arg, uptr value, bool success) {
    (void)value;
    assert(success);
    Thread *thread = arg;
    if (thread->proc != NULL) {
        // Acknowledge only after switching away, including the VMM context
        proc_remove_thread(thread->proc, thread);
    }
    thread_rel(thread);
}

void thread_exit_nolock(Thread *thread) {
    assert(thread == cpu_get_thread());
    assert(__atomic_load_n(&thread->state, __ATOMIC_RELAXED) ==
           THREAD_STATE_RUNNING);
    __atomic_store_n(&thread->exit_requested, true, __ATOMIC_RELEASE);
    thread_transition_nolock(thread, THREAD_STATE_EXITED);
    sched_yield_nolock(
        false,
        (ContextMessage){.callback = thread_exit_complete, .arg = thread});
    __builtin_unreachable();
}

void thread_destroy(Thread *thread) {
    assert(__atomic_load_n(&thread->state, __ATOMIC_RELAXED) ==
           THREAD_STATE_EXITED);
    assert(thread->cpu == NULL && thread->waiting_on == NULL);
    thread_rel(thread);
}

void thread_ready_nolock(Thread *thread) {
    Cpu *ready_cpu = thread->last_cpu;
    assert(thread->waiting_on == NULL);
    assert(thread->sched_lock == &ready_cpu->runqueue_lock);
    thread_transition_nolock(thread, THREAD_STATE_READY);
    TAILQ_INSERT_TAIL(&ready_cpu->sched_queue, thread, threads);

    log_debug("thread with tid=%zu is now runnable on cpu %zu", thread->tid,
              ready_cpu->id);

    cpu_send_ipi(ready_cpu, CPU_IPI_RESCHEDULE);
}

void thread_ready(Thread *thread) {
    thread_ready_on_cpu(thread, smp_get_random());
}

void thread_ready_on_cpu(Thread *thread, Cpu *next) {
    Cpu *old = thread->last_cpu;
    // This entry point publishes a NEW thread, still owned by its creator
    assert(__atomic_load_n(&thread->state, __ATOMIC_RELAXED) ==
           THREAD_STATE_NEW);
    Cpu *first = old->id < next->id ? old : next;
    Cpu *second = old->id < next->id ? next : old;
    spinlock_acq(&first->runqueue_lock);
    if (first != second) spinlock_acq(&second->runqueue_lock);
    thread->last_cpu = next;
    __atomic_store_n(&thread->sched_lock, &next->runqueue_lock,
                     __ATOMIC_RELEASE);
    thread_ready_nolock(thread);
    if (first != second) spinlock_rel(&second->runqueue_lock);
    spinlock_rel(&first->runqueue_lock);
}
