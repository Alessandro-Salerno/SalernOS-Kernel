#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <signal.h>

// SYSCALL: sigaction(int sig, struct sigaction *act, struct sigaction *oact)
SYSCALL(syscall_sigaction) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int sig = SYSCALL_ARG(int, 1);
    SignalAction *act = SYSCALL_ARG(void *, 2);
    SignalAction *oact = SYSCALL_ARG(void *, 3);

    if (sig < 1 || sig >= NSIG) return SYSCALL_ERR(EINVAL);

    if (act != NULL && (sig == SIGKILL || sig == SIGSTOP)) {
        return SYSCALL_ERR(EINVAL);
    }

    SignalAction *new_act = NULL;
    if (act != NULL) {
        new_act = slab_alloc(sizeof(SignalAction));
        int copy_ret = memcpy_from_user(new_act, act, sizeof(*new_act));
        // According to clankers, Linux does not error in this case. However,
        // there's no good reason for which a userspace process would want to
        // give us a non-user or otherwise invalid address, so we save the
        // assembly trickery and punish these gangsters with an error instead
        if (copy_ret == 0 && !MMU_ADDR_IS_USER(new_act->sa_action)) {
            copy_ret = EFAULT;
        }
        if (copy_ret != 0) {
            slab_free(new_act, sizeof(*new_act));
            return SYSCALL_ERR(copy_ret);
        }
    }

    Proc *curr_proc = cpu_get_thread()->proc;
    SignalAction old_act = {.sa_action = SIG_DFL};
    SignalAction *to_free = NULL;
    spinlock_acq(&curr_proc->signal_lock);

    if (curr_proc->sigaction[sig] != NULL) old_act = *curr_proc->sigaction[sig];

    if (new_act != NULL) {
        to_free = curr_proc->sigaction[sig];
        curr_proc->sigaction[sig] = new_act;
    }

    spinlock_rel(&curr_proc->signal_lock);

    if (to_free != NULL) slab_free(to_free, sizeof(SignalAction));

    if (oact != NULL) {
        int copy_ret = memcpy_to_user(oact, &old_act, sizeof(old_act));
        if (copy_ret != 0) return SYSCALL_ERR(copy_ret);
    }

    return SYSCALL_OK(0);
}
