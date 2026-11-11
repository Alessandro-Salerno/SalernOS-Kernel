#include <kernel/com/sync/workqueue.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>

// NOTE: kernel threads have no process to deliver signals to, so waits must be
// uninterruptible

static void worker_main(WorkQueue *wq) {
    spinlock_acq(&wq->lock);

    while (true) {
        Work *work = TAILQ_FIRST(&wq->works);
        if (work == NULL) {
            spinlock_wait(&wq->lock, &wq->waitlist, 0, false);
            continue;
        }

        TAILQ_REMOVE(&wq->works, work, queue);
        work->pending = false;
        spinlock_rel(&wq->lock);
        work->handler(work);
        spinlock_acq(&wq->lock);
    }
}

void workqueue_init(WorkQueue *wq) {
    wq->lock = SPINLOCK_NEW();
    TAILQ_INIT(&wq->works);
    waitlist_init(&wq->waitlist);
    wq->worker = thread_new_kernel(NULL, worker_main, wq);
    thread_ready(wq->worker);
}

void workqueue_push(WorkQueue *wq, Work *work) {
    spinlock_acq(&wq->lock);
    if (!work->pending) {
        work->pending = true;
        TAILQ_INSERT_TAIL(&wq->works, work, queue);
        sched_notify(&wq->waitlist);
    }
    spinlock_rel(&wq->lock);
}
