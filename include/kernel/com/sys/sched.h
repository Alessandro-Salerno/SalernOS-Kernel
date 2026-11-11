#pragma once

#include <arch/context.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>

// On EINTR authority remains held and on success it is released on switch-in
int sched_yield_nolock(bool interruptible, ContextMessage message);
void sched_switch_finish(ContextMessage message);

void sched_yield(void);
void sched_isr(InterruptHandler *isr, Context *ctx);

// prepare precedes authority and release follows wait publication under
// authority acquire runs outside authority and restores the primitive on every
// return The message callback receives success=true on switch-in, or false
// after failed-yield rollback and authority release, before reacquiring the
// primitive
typedef struct SchedWaitOps {
    void (*prepare)(void *primitive);
    ContextMessage (*release)(void *primitive);
    void (*acquire)(void *primitive);
} SchedWaitOps;

int sched_wait(Waitlist *waitlist, u64 timeout, bool interruptible,
               const SchedWaitOps *ops, void *primitive);
void sched_wait_nodrop(Waitlist *waitlist);
int sched_wait_nodrop_timeout(Waitlist *waitlist, u64 timeout);

void sched_notify(Waitlist *waitlist);
void sched_notify_all(Waitlist *waitlist);
void sched_notify_n(Waitlist *waitlist, usize num_waiters);
void sched_interrupt_nolock(Thread *thread);
void sched_notify_thread_nolock(Thread *thread);
void sched_notify_thread(Thread *thread);

void sched_prioritize(Thread *thread);
void sched_prioritize_nolock(Thread *thread);

void sched_init(void);
