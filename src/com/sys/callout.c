#include <arch/cpu.h>
#include <arch/info.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>

// CREDIT: vloxei64/ke
// CREDIT: FreeBSD

#if CONFIG_CALLOUT_MODE == CONST_CALLOUT_PER_CPU
#define GET_CALLOUT() (&cpu_get()->callout)
#elif CONFIG_CALLOUT_MODE == CONST_CALLOUT_ONLY_BSP
#define GET_CALLOUT() (g_bsp_callout)
#else
#error "unknown callout mode"
#endif

static CalloutQueue *g_bsp_callout;

static void enqueue_callout(CalloutQueue *cpu_callout, Callout *callout) {
    // Add callout to the queue so that it is ordered
    bool added = false;
    Callout *curr = TAILQ_FIRST(&cpu_callout->queue);
    while (curr != NULL) {
        if (callout->ns < curr->ns) {
            TAILQ_INSERT_BEFORE(curr, callout, queue);
            added = true;
            break;
        }
        curr = TAILQ_NEXT(curr, queue);
    }

    // If the new callout was not added by the previous block, it means that its
    // delay is higher than that of any other callout in the queue, so it needs
    // to be placed at the end
    if (!added) TAILQ_INSERT_TAIL(&cpu_callout->queue, callout, queue);

    spinlock_acq(&callout->entry_lock);
    callout->cpu_callout = cpu_callout;
    spinlock_rel(&callout->entry_lock);
}

static void callout_queue_add_at(CalloutQueue *cpu_callout, CalloutFn handler,
                                 void *arg, u64 ns) {
    Callout *new = slab_alloc(sizeof(Callout));
    new->handler = handler;
    new->arg = arg;
    new->ns = ns;
    new->reuse = false;

    spinlock_acq(&cpu_callout->lock);
    enqueue_callout(cpu_callout, new);
    spinlock_rel(&cpu_callout->lock);
}

// NOTE: repeated code for locking reasons
static void callout_queue_add(CalloutQueue *cpu_callout, CalloutFn handler,
                              void *arg, u64 delay) {
    spinlock_acq(&cpu_callout->lock);
    Callout *new = slab_alloc(sizeof(Callout));
    new->handler = handler;
    new->arg = arg;
    new->ns = cpu_callout->ns + delay;
    new->reuse = false;

    enqueue_callout(cpu_callout, new);
    spinlock_rel(&cpu_callout->lock);
}

u64 callout_get_time(void) {
    CalloutQueue *callout = GET_CALLOUT();
    spinlock_acq(&callout->lock);
    u64 time = callout->ns;
    spinlock_rel(&callout->lock);
    return time;
}

void callout_run(void) {
    Cpu *curr_cpu = cpu_get();
    CalloutQueue *cpu_callout = &curr_cpu->callout;

    spinlock_acq(&cpu_callout->lock);
    cpu_callout->ns += TIMER_NS;

    while (true) {
#if CONFIG_CALLOUT_MODE == CONST_CALLOUT_ONLY_BSP
        if (cpu_callout != g_bsp_callout) break;
#endif

        Callout *callout = TAILQ_FIRST(&cpu_callout->queue);

        if (callout == NULL || callout->ns > cpu_callout->ns) break;

        callout->reuse = false;
        spinlock_acq(&callout->entry_lock);
        TAILQ_REMOVE_HEAD(&cpu_callout->queue, queue);
        bool snapshot = callout->snapshot;
        Callout firing = {.handler = callout->handler,
                          .arg = callout->arg,
                          .value = callout->value};
        assert(!snapshot || callout->permanent);
        callout->cpu_callout = NULL;
        spinlock_rel(&callout->entry_lock);

        spinlock_rel(&cpu_callout->lock);
        firing.handler(snapshot ? &firing : callout);
        spinlock_acq(&cpu_callout->lock);

        if (!snapshot && !callout->reuse && !callout->permanent) {
            slab_free(callout, sizeof(Callout));
        }
    }

    bool resched = false;
    if (cpu_callout->ns >= cpu_callout->next_preempt) {
        resched = true;
        cpu_callout->next_preempt += SCHED_NS;
    }

    spinlock_rel(&cpu_callout->lock);

    if (resched) sched_yield();
}

void callout_isr(InterruptHandler *isr, Context *ctx) {
    (void)isr;
    (void)ctx;
    callout_run();
}

void callout_reschedule_at(Callout *callout, u64 ns) {
    CalloutQueue *cpu_callout = GET_CALLOUT();
    callout->reuse = true;
    callout->ns = ns;

    spinlock_acq(&cpu_callout->lock);
    enqueue_callout(cpu_callout, callout);
    spinlock_rel(&cpu_callout->lock);
}

// NOTE: repeated code for locking reasons
void callout_reschedule(Callout *callout, u64 delay) {
    CalloutQueue *cpu_callout = GET_CALLOUT();
    spinlock_acq(&cpu_callout->lock);
    callout->reuse = true;
    callout->ns = cpu_callout->ns + delay;
    enqueue_callout(cpu_callout, callout);
    spinlock_rel(&cpu_callout->lock);
}

void callout_add_at(CalloutFn handler, void *arg, u64 ns) {
    callout_queue_add_at(GET_CALLOUT(), handler, arg, ns);
}

void callout_add(CalloutFn handler, void *arg, u64 delay) {
    callout_queue_add(GET_CALLOUT(), handler, arg, delay);
}

void callout_add_at_bsp(CalloutFn handler, void *arg, u64 ns) {
    callout_queue_add_at(g_bsp_callout, handler, arg, ns);
}

void callout_add_bsp(CalloutFn handler, void *arg, u64 delay) {
    callout_queue_add(g_bsp_callout, handler, arg, delay);
}

void callout_set_bsp_nolock(CalloutQueue *bsp_queue) {
    g_bsp_callout = bsp_queue;
}

Callout *callout_new(CalloutFn handler, void *arg, u64 ns) {
    Callout *new = slab_alloc(sizeof(Callout));
    new->handler = handler;
    new->arg = arg;
    new->ns = ns;
    return new;
}

void callout_destroy(Callout *callout) {
    callout_cancel(callout);
    slab_free(callout, sizeof(Callout));
}

void callout_enqueue(Callout *callout) {
    CalloutQueue *cpu_callout = GET_CALLOUT();
    spinlock_acq(&cpu_callout->lock);
    enqueue_callout(cpu_callout, callout);
    spinlock_rel(&cpu_callout->lock);
}

// NOTE: this seems weird, but it's used for pre-allocated callouts, such as
// those used by the scheduler's timed wait primtives
void callout_set_and_enqueue(Callout *callout, CalloutFn handler, u64 delay) {
    CalloutQueue *cpu_callout = GET_CALLOUT();
    callout->handler = handler;
    spinlock_acq(&cpu_callout->lock);
    callout->ns = cpu_callout->ns + delay;
    enqueue_callout(cpu_callout, callout);
    spinlock_rel(&cpu_callout->lock);
}

// Callers serialize arming against cancellation. A handler already detached
// from the queue owns its firing and snapshot handlers need no cancellation
// join
bool callout_cancel(Callout *callout) {
    spinlock_acq(&callout->entry_lock);
    CalloutQueue *queue = callout->cpu_callout;
    spinlock_rel(&callout->entry_lock);
    if (queue == NULL) return false;
    spinlock_acq(&queue->lock);
    spinlock_acq(&callout->entry_lock);
    bool removed = queue == callout->cpu_callout;
    if (removed) {
        TAILQ_REMOVE(&queue->queue, callout, queue);
        callout->cpu_callout = NULL;
    }
    spinlock_rel(&callout->entry_lock);
    spinlock_rel(&queue->lock);
    return removed;
}
