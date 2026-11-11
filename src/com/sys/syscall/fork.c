#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/platform/context.h>
#include <kernel/platform/mmu.h>

// SYSCALL: fork()
SYSCALL(syscall_fork) {
    SYSCALL_UNUSED_START(1);

    Context *ctx = SYSCALL_CONTEXT();

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    VMMContext *new_vmm_ctx = vmm_duplicate_context(curr_proc->vmm_context);
    mmu_invalidate(curr_proc->vmm_context->pagetable, NULL, 512);
    Vnode *cwd = proc_get_cwd(curr_proc);
    Proc *new_proc = proc_new(new_vmm_ctx, curr_proc->pid, curr_proc->root,
                              cwd);
    vfs_vnode_rel(cwd);
    // NOTE: process group is inherited from parent in proc_new
    Thread *new_thread = thread_new(new_proc, NULL, 0, 0);
    CONTEXT_FORK_EXTRA(new_thread->xctx);
    assert(new_thread != NULL);
    assert(new_proc != NULL);
    assert(new_vmm_ctx != NULL);
    assert(new_vmm_ctx->pagetable != NULL);

    spinlock_acq(&curr_proc->fd_lock);
    new_proc->next_fd = curr_proc->next_fd;
    new_proc->max_fd = curr_proc->max_fd;
    for (int i = 0; i < curr_proc->max_fd; i++) {
        if (curr_proc->fd[i].file != NULL) {
            new_proc->fd[i].flags = curr_proc->fd[i].flags;
            FILE_HOLD(curr_proc->fd[i].file);
            new_proc->fd[i].file = curr_proc->fd[i].file;
        }
    }
    spinlock_rel(&curr_proc->fd_lock);

    CONTEXT_FORK(new_thread, *ctx);
    __atomic_add_fetch(&curr_proc->num_children, 1, __ATOMIC_RELAXED);

    spinlock_acq(&curr_proc->signal_lock);
    new_thread->masked_signals = curr_thread->masked_signals;
    for (usize i = 0; i < NSIG; i++) {
        if (curr_proc->sigaction[i] != NULL) {
            SignalAction *sa = slab_alloc(sizeof(SignalAction));
            *sa = *curr_proc->sigaction[i];
            new_proc->sigaction[i] = sa;
        }
    }
    spinlock_rel(&curr_proc->signal_lock);

    thread_ready(new_thread);

    return SYSCALL_OK(new_proc->pid);
}
