#include <arch/cpu.h>
#include <arch/info.h>
#include <arch/mmu.h>
#include <errno.h>
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
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>

#include <salernos/syscall_str.h>

// TODO: I used to take sched_lock here because back in 0.2.3 not doing it
// somehow crashed the kernel sometimes, but now the opposite seems to happen:
// use icewm, when passing on the main bar, it does execve for some reason and
// has CLOEXEC on a socket, so the socket sybsystem notifies readers with
// sched_notify_all which also takes the sched_lock. So I need to check
// which version is correct.

// SYSCALL: execve(const struct syscall_str *path, const struct syscall_str
// *argv, usize argc, const struct syscall_str *envp, usize envc)
SYSCALL(syscall_execve) {
    SYSCALL_UNUSED_START(6);

    Context *ctx = SYSCALL_CONTEXT();
    const struct syscall_str *upath = SYSCALL_ARG(void *, 1);
    const struct syscall_str *argv = SYSCALL_ARG(void *, 2);
    usize argc = SYSCALL_ARG(usize, 3);
    const struct syscall_str *envp = SYSCALL_ARG(void *, 4);
    usize envc = SYSCALL_ARG(usize, 5);

    char path[CONFIG_PATH_MAX];
    usize pathlen;
    int path_ret = syscall_copy_str(path, sizeof(path), upath, &pathlen);
    if (path_ret != 0) return SYSCALL_ERR(path_ret);

    // The strings themselves are checked while they are copied
    if (!MMU_ARRAY_IS_USER(argv, argc, sizeof(*argv)) ||
        !MMU_ARRAY_IS_USER(envp, envc, sizeof(*envp))) {
        return SYSCALL_ERR(EFAULT);
    }

    Thread *thread = cpu_get_thread();
    Proc *proc = thread->proc;

    VMMContext *old_vmm_ctx = proc->vmm_context;
    VMMContext *new_vmm_ctx = NULL;
    int status = elf64_prepare_proc(path, pathlen, argv, argc, envp, envc, proc,
                                    ctx, &new_vmm_ctx);

    if (status != 0) return SYSCALL_ERR(status);

    proc_kill_other_threads();

    // NOTE: we need preemption to be disabled here because otherwise this could
    // happen:
    // 1. switch to new context
    // 2. preempted (proc-vmm_context = old_vmm_context still)
    // 3. return here (with old context)
    // 4. destroy old context (casuing kernel panic because it's in use)
    // We don't need a lock because proc->vmm_context is now effectively just
    // ours (since we killed all other threads) and noone looking up this PID is
    // going to read the vmm_context field, so no need to do anything atomically
    // or introduce locks
    cpu_disable_interrupts();
    vmm_switch(new_vmm_ctx);
    proc->vmm_context = new_vmm_ctx;
    assert(proc->num_ref > 0);
    cpu_enable_interrupts();

    vmm_destroy_context(old_vmm_ctx);

    // NOTE: we don't need to take locks here because we're guaranteed to be the
    // only running thread of this process. Thus process-local resources are
    // de-facto thread-local, and this is even more true because these specific
    // resources are only altered by system calls issued by the process (which,
    // however, is stuck here)

    for (int i = 0; i < proc->max_fd; i++) {
        if (proc->fd[i].file != NULL &&
            ((proc->fd[i].flags & FD_CLOEXEC) ||
             (proc->fd[i].file->flags & O_CLOEXEC))) {
            FILE_REL(proc->fd[i].file);
            proc->fd[i] = (FileDesc){0};
        }
    }

    for (usize i = 0; i < NSIG; i++) {
        if (proc->sigaction[i] != NULL &&
            (i != SIGCHLD || proc->sigaction[i]->sa_action != SIG_IGN)) {
            slab_free(proc->sigaction[i], sizeof(SignalAction));
            proc->sigaction[i] = NULL;
        }
    }

    CONTEXT_INIT_EXTRA(thread->xctx);
    CONTEXT_RESTORE_EXTRA(thread->xctx);

    proc->did_execve = true;

    return SYSCALL_OK(0);
}
