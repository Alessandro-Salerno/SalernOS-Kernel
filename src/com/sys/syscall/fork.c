#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/platform/context.h>
#include <kernel/platform/mmu.h>

_Static_assert(NSIG * sizeof(SignalAction *) <= PAGE_SIZE,
               "fork copies the signal actions through the scratch page");

// Frees an unpublished thread through the reaper
static void discard_thread(Thread *thread) {
    thread_transition(thread, THREAD_STATE_EXITED);
    thread_destroy(thread);
}

// SYSCALL: fork()
SYSCALL(syscall_fork) {
    SYSCALL_UNUSED_START(1);

    Context *ctx = SYSCALL_CONTEXT();

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    // Everything that can fail happens before proc_new publishes the child
    VMMContext *new_vmm_ctx = vmm_duplicate_context(curr_proc->vmm_context);
    mmu_invalidate(curr_proc->vmm_context->pagetable, NULL, 512);
    if (new_vmm_ctx == NULL) return SYSCALL_ERR(ENOMEM);

    Thread *new_thread = thread_new(NULL, NULL, 0, 0);
    if (new_thread == NULL) {
        vmm_destroy_context(new_vmm_ctx);
        return SYSCALL_ERR(ENOMEM);
    }

    SignalAction **sigaction = curr_thread->scratch;
    bool sigaction_ok = true;
    spinlock_acq(&curr_proc->signal_lock);
    new_thread->masked_signals = curr_thread->masked_signals;
    for (usize i = 0; i < NSIG; i++) {
        sigaction[i] = NULL;
        if (sigaction_ok && curr_proc->sigaction[i] != NULL) {
            sigaction[i] = slab_alloc(sizeof(SignalAction));
            if (sigaction[i] == NULL) sigaction_ok = false;
            else *sigaction[i] = *curr_proc->sigaction[i];
        }
    }
    spinlock_rel(&curr_proc->signal_lock);

    Proc *new_proc = NULL;
    int ret = ENOMEM;
    if (sigaction_ok) {
        Vnode *cwd = proc_get_cwd(curr_proc);
        Vnode *root = proc_get_root(curr_proc);
        // NOTE: process group is inherited from parent in proc_new
        ret = proc_new(new_vmm_ctx, curr_proc->pid, root, cwd, &new_proc);
        vfs_vnode_rel(cwd);
        vfs_vnode_rel(root);
    }

    if (ret != 0) {
        for (usize i = 0; i < NSIG; i++) {
            slab_free(sigaction[i], sizeof(SignalAction));
        }
        discard_thread(new_thread);
        vmm_destroy_context(new_vmm_ctx);
        return SYSCALL_ERR(ret);
    }

    new_thread->proc = new_proc;
    proc_add_thread(new_proc, new_thread);
    CONTEXT_FORK_EXTRA(new_thread->xctx);
    for (usize i = 0; i < NSIG; i++) new_proc->sigaction[i] = sigaction[i];

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

    thread_ready(new_thread);

    return SYSCALL_OK(new_proc->pid);
}
