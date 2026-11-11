#pragma once

#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sync/waitlist.h>
#include <kernel/com/util.h>
#include <vendor/tailq.h>

typedef struct Work {
    void (*handler)(struct Work *work);
    TAILQ_ENTRY(Work) queue;
    bool pending;
} Work;

typedef struct WorkQueue {
    Spinlock lock;
    TAILQ_HEAD(, Work) works;
    Waitlist waitlist;
    struct Thread *worker;
} WorkQueue;

void workqueue_init(WorkQueue *wq);
void workqueue_push(WorkQueue *wq, Work *work);

static inline void work_init(Work *work, void (*handler)(Work *work)) {
    work->handler = handler;
    work->pending = false;
}
