#pragma once

#include <arch/context.h>
#include <kernel/com/util.h>
#include <signal.h>
#include <sys/types.h>

// NOTE: These must be used in a thread-safe manner, i.e. while holding the
// process'es signal_lock, since they're no longer atomic

#define SIGNAL_NONE (-1)

typedef struct {
    unsigned long sig[1024 / (8 * sizeof(long))];
} SignalSet;

// Sigmask is essentially an entry into the sig array above
// The struct above is kept for compatibility with Linux (and mlibc currently),
// but there's no need for all those signals.
// So, to save some space in proc and thread structs, we use sigmask and only
// ever access sigset_t.sig[0]
typedef unsigned long SigMask;

static inline void signal_sigmask_init(SigMask *mask) { *mask = 0; }
static inline void signal_sigmask_set(SigMask *mask, int sig) {
    *mask |= 1UL << (sig - 1);
}
static inline void signal_sigmask_unset(SigMask *mask, int sig) {
    *mask &= ~(1UL << (sig - 1));
}
static inline bool signal_sigmask_isset(const SigMask *mask, int sig) {
    return *mask & (1UL << (sig - 1));
}

typedef struct SignalAction {
    void (*sa_action)(int);
    // void (*sa_action)(int, siginfo_t *, void *);
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    SignalSet sa_mask;
} SignalAction;

typedef struct SignalUContext {
    unsigned long uc_flags;
    struct SignalUContext *uc_link;
    stack_t uc_stack;
    mcontext_t uc_mcontext;
    SignalSet uc_sigmask;
} SignalUContext;

typedef struct SigFrame {
    siginfo_t info;
    SignalUContext uc;
} SigFrame;

#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/thread.h>

void signal_sigset_empty(SignalSet *set);
int signal_send_to_proc(pid_t pid, int sig, struct Proc *sender);
int signal_send_to_proc_group(pid_t pgid, int sig, struct Proc *sender);
// Publish under signal_lock, then call kick after dropping it. The caller
// keeps the thread alive across both operations
void signal_kick(struct Thread *thread);
int signal_send_to_thread_nolock(struct Thread *thread, int sig,
                                 struct Proc *sender);
int signal_send_to_thread(struct Thread *thread, int sig, struct Proc *sender);
void signal_dispatch(Context *ctx, struct Thread *thread);
int signal_set_mask_nolock(SigMask *mask, int how, SignalSet *set,
                           SignalSet *oset);
int signal_set_mask(SigMask *mask, int how, SignalSet *set, SignalSet *oset,
                    Spinlock *signal_lock);
int signal_check_nolock(void);
int signal_check(void);
