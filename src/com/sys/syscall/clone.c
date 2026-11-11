#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <kernel/platform/context.h>
#include <stdatomic.h>
#include <stdio.h>

// SYSCALL: clone(void *new_ip, void *new_sp)
SYSCALL(syscall_clone) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    void *new_ip = SYSCALL_ARG(void *, 1);
    void *new_sp = SYSCALL_ARG(void *, 2);

    if (!MMU_ADDR_IS_USER(new_ip)) return SYSCALL_ERR(EFAULT);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    Thread *new_thread = thread_new(curr_proc, NULL, 0, NULL);
    CONTEXT_CLONE(new_thread, new_ip, new_sp);
    CONTEXT_INIT_EXTRA(new_thread->xctx);

    spinlock_acq(&curr_proc->signal_lock);
    new_thread->masked_signals = curr_thread->masked_signals;
    spinlock_rel(&curr_proc->signal_lock);

    thread_ready(new_thread);

    return SYSCALL_OK(new_thread->tid);
}
