#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>

// SYSCALL: sigpending(sigset_t *set)
SYSCALL(syscall_sigpending) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(2);

    SignalSet *sigset = SYSCALL_ARG(SignalSet *, 1);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    SignalSet kset;
    signal_sigset_empty(&kset);
    spinlock_acq(&curr_proc->signal_lock);
    kset.sig[0] = curr_thread->pending_signals | curr_proc->pending_signals;
    spinlock_rel(&curr_proc->signal_lock);
    int copy_ret = memcpy_to_user(sigset, &kset, sizeof(kset));
    if (copy_ret != 0) return SYSCALL_ERR(copy_ret);

    return SYSCALL_OK(0);
}
