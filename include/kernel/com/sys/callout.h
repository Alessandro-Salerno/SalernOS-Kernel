#pragma once

#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/util.h>
#include <vendor/tailq.h>

typedef struct Callout Callout;
typedef void (*CalloutFn)(Callout *callout);

typedef struct Callout {
    u64 ns;
    CalloutFn handler;
    void *arg;
    bool reuse;
    bool permanent;
    // Snapshot callouts are permanent reusable slots. Dispatch copies arg/value
    // before releasing the queue lock and never touches the slot afterwards
    bool snapshot;
    uptr value;

    Spinlock entry_lock;              // only protects the following fields
    struct CalloutQueue *cpu_callout; // NULL if not scheduled
    TAILQ_ENTRY(Callout) queue;
} Callout;

TAILQ_HEAD(CalloutTailQ, Callout);

typedef struct CalloutQueue {
    u64 ns;
    u64 next_preempt;
    struct CalloutTailQ queue;
    Spinlock lock;
} CalloutQueue;

u64 callout_get_time(void);
void callout_run(void);
void callout_isr(InterruptHandler *isr, Context *ctx);
void callout_reschedule_at(Callout *callout, u64 ns);
void callout_reschedule(Callout *callout, u64 delay);
void callout_add_at(CalloutFn handler, void *arg, u64 ns);
void callout_add(CalloutFn handler, void *arg, u64 delay);
void callout_add_at_bsp(CalloutFn handler, void *arg, u64 ns);
void callout_add_bsp(CalloutFn handler, void *arg, u64 delay);
void callout_set_bsp_nolock(CalloutQueue *bsp_queue);
Callout *callout_new(CalloutFn handler, void *arg, u64 ns);
void callout_destroy(Callout *callout);
void callout_enqueue(Callout *callout);
void callout_set_and_enqueue(Callout *callout, CalloutFn handler, u64 delay);
// Returns true only if this call removed a queued firing
// A detached handler runs independently
bool callout_cancel(Callout *callout);
