#include <arch/cpu.h>
#include <kernel/com/sync/workqueue.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>

// NOTE: kernel threads have no process to deliver signals to, so waits must
// be uninterruptible

static void run_work(WorkQueue *wq, Work *work) {
    TAILQ_REMOVE(&wq->works, work, queue);
    work->pending = false;
    wq->active++;
    spinlock_rel(&wq->lock);
    work->handler(work);
    spinlock_acq(&wq->lock);
    wq->active--;
    if (wq->active == 0 && TAILQ_EMPTY(&wq->works))
        sched_notify_all(&wq->drain_waitlist);
}

static void worker_main(WorkQueue *wq) {
    spinlock_acq(&wq->lock);
    for (;;) {
        Work *work = TAILQ_FIRST(&wq->works);
        if (work != NULL) {
            run_work(wq, work);
            continue;
        }
        if (wq->stopping) {
            wq->worker = NULL;
            sched_notify_all(&wq->drain_waitlist);
            spinlock_rel(&wq->lock);
            thread_exit(cpu_get_thread());
            __builtin_unreachable();
        }
        spinlock_wait(&wq->lock, &wq->work_waitlist, 0, false);
    }
}

void workqueue_init(WorkQueue *wq, Cpu *cpu) {
    wq->lock = SPINLOCK_NEW();
    TAILQ_INIT(&wq->works);
    waitlist_init(&wq->work_waitlist);
    waitlist_init(&wq->drain_waitlist);
    wq->active = 0;
    wq->stopping = false;
    wq->worker = thread_new_kernel(NULL, worker_main, wq);
    if (cpu != NULL) thread_ready_on_cpu(wq->worker, cpu);
    else thread_ready(wq->worker);
}

void workqueue_push(WorkQueue *wq, Work *work) {
    spinlock_acq(&wq->lock);
    assert(!wq->stopping);
    if (!work->pending) {
        work->pending = true;
        TAILQ_INSERT_TAIL(&wq->works, work, queue);
        sched_notify(&wq->work_waitlist);
    }
    spinlock_rel(&wq->lock);
}

void workqueue_drain(WorkQueue *wq) {
    Thread *curr_thread = cpu_get_thread();
    spinlock_acq(&wq->lock);
    assert(curr_thread == NULL || wq->worker != curr_thread);
    assert(curr_thread != NULL || wq->active == 0);

    while (!TAILQ_EMPTY(&wq->works) || wq->active != 0) {
        // Without a thread we cannot sleep, so do the work ourselves
        if (curr_thread != NULL)
            spinlock_wait(&wq->lock, &wq->drain_waitlist, 0, false);
        else run_work(wq, TAILQ_FIRST(&wq->works));
    }

    // Note that it is not possible to both have in-flight work items and a NULL
    // current thread, as curr_thread == NULL implies setup context, whichh
    // implies no preemption and no parallelism, so there's no way the system
    // could be simultaneously running the work item AND the drain

    spinlock_rel(&wq->lock);
}

void workqueue_destroy(WorkQueue *wq) {
    workqueue_drain(wq);
    spinlock_acq(&wq->lock);
    wq->stopping = true;
    sched_notify(&wq->work_waitlist);
    while (wq->worker != NULL) {
        spinlock_wait(&wq->lock, &wq->drain_waitlist, 0, false);
    }
    spinlock_rel(&wq->lock);
}
