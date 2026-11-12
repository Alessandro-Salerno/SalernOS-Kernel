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
    Waitlist work_waitlist;
    Waitlist drain_waitlist;
    struct Thread *worker;
    usize active;
    bool stopping;
} WorkQueue;

struct Cpu;
void workqueue_init(WorkQueue *wq, struct Cpu *cpu);
void workqueue_push(WorkQueue *wq, Work *work);
void workqueue_drain(WorkQueue *wq);
void workqueue_destroy(WorkQueue *wq);

static inline void work_init(Work *work, void (*handler)(Work *work)) {
    work->handler = handler;
    work->pending = false;
}
