#pragma once

#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sync/waitlist.h>
#include <kernel/com/util.h>
#include <vendor/tailq.h>

SLIST_HEAD(ThreadSList, Thread);

typedef enum ThreadState {
    THREAD_STATE_NEW = 0,
    THREAD_STATE_WAITING,
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_EXITED
} ThreadState;

#include <arch/context.h>
#include <arch/cpu.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/proc.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>

typedef struct ThreadTimer {
    Spinlock lock;
    struct itimerval itimerval;
    u64 ctime;        // when it was created
    void *itimer_arg; // see src/com/sys/syscall/setitimer.c
} ThreadTimer;

typedef struct Thread {
    void *kernel_stack;
    Context ctx;
    ContextExtra xctx;
    Spinlock *sched_lock;
    uptr epoch;
    usize refs;
    SLIST_ENTRY(Thread) zombies;
    bool exit_requested;
    bool signal_stopped; // protected by proc->signal_lock
    bool wait_interruptible;
    struct Proc *proc;
    struct Cpu *cpu;
    struct Cpu *last_cpu;
    ThreadState state; // relaxed atomic accesses
    TAILQ_ENTRY(Thread) threads;
    TAILQ_ENTRY(Thread) proc_threads;
    int lock_depth;
    pid_t tid;

    Context *usercopy_ctx;
    // One page for copies of syscall arguments, valid until the syscall returns
    void *scratch;

    Waitlist *waiting_on;
    u64 ctime;
    u64 time_slept;
    struct Callout *timed_wait_callout;
    int wait_error;

    SigMask pending_signals;
    SigMask masked_signals;

    // Each outstanding interval-timer callout owns a thread reference
    ThreadTimer real_timer;
} Thread;

Thread *thread_new(struct Proc *proc, void *stack, u64 stack_size, void *entry);
Thread *thread_new_kernel(struct Proc *proc, void *entry, void *arg);

void thread_lock(Thread *thread);
void thread_unlock(Thread *thread);
void thread_hold(Thread *thread);
void thread_rel(Thread *thread);
void thread_reap(void);

void thread_transition(Thread *thread, ThreadState state);
void thread_transition_nolock(Thread *thread, ThreadState state);
void thread_request_exit(Thread *thread);
void thread_exit(Thread *thread);
void thread_exit_nolock(Thread *thread);
// Release a caller-owned reference after switch-out and final freeing is
// deferred
void thread_destroy(Thread *thread);
void thread_ready_nolock(Thread *thread);
void thread_ready(Thread *thread);
