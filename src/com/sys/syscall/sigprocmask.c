#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>

// SYSCALL: sigprocmask(int how, sigset_t *set, sigset_t *oset)
SYSCALL(syscall_sigprocmask) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int how = SYSCALL_ARG(int, 1);
    SignalSet *set = SYSCALL_ARG(SignalSet *, 2);
    SignalSet *oset = SYSCALL_ARG(SignalSet *, 3);

    SignalSet kset;
    SignalSet koset;
    SignalSet *kset_ptr = NULL;
    if (set != NULL) {
        int copy_ret = memcpy_from_user(&kset, set, sizeof(kset));
        if (copy_ret != 0) return SYSCALL_ERR(copy_ret);
        kset_ptr = &kset;
    }

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    int sig_ret = signal_set_mask(&curr_thread->masked_signals, how, kset_ptr,
                                  oset != NULL ? &koset : NULL,
                                  &curr_proc->signal_lock);

    if (sig_ret != 0) return SYSCALL_ERR(sig_ret);

    if (oset != NULL) {
        int copy_ret = memcpy_to_user(oset, &koset, sizeof(koset));
        if (copy_ret != 0) return SYSCALL_ERR(copy_ret);
    }

    return SYSCALL_OK(0);
}
