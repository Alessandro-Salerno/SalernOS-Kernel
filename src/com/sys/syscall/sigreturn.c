#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/context.h>

// SYSCALL: sigreturn()
SYSCALL(syscall_sigreturn) {
    SYSCALL_UNUSED_START(1);

    Context *ctx = SYSCALL_CONTEXT();

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    SigFrame sframe;
    if (context_restore_sigframe(&sframe, ctx) != 0) {
        proc_terminate(0, SIGSEGV);
        __builtin_unreachable();
    }

    spinlock_acq(&curr_proc->signal_lock);
    curr_thread->masked_signals = sframe.uc.uc_sigmask.sig[0];
    spinlock_rel(&curr_proc->signal_lock);

    return SYSCALL_DISCARD();
}
