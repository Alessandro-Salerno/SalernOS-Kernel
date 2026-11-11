#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALL: getpgid(pid_t pid)
SYSCALL(syscall_getpgid) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(2);

    pid_t pid = SYSCALL_ARG(pid_t, 1);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    if (pid == 0) {
        spinlock_acq(&curr_proc->pg_lock);
        assert(curr_proc->proc_group != NULL);
        pid_t pgid = curr_proc->proc_group->pgid;
        log_debug("pid=%d has pgid=%d", curr_proc->pid, pgid);
        spinlock_rel(&curr_proc->pg_lock);
        return SYSCALL_OK(pgid);
    }

    if (pid < 1) return SYSCALL_ERR(EINVAL);

    Proc *proc = proc_get_by_pid(pid);

    if (proc == NULL) return SYSCALL_ERR(ESRCH);

    SyscallRet ret = SYSCALL_BASE_ERR();

    assert(curr_proc->proc_group != NULL);
    assert(proc->proc_group != NULL);

    spinlock_acq(&curr_proc->pg_lock);
    if (proc != curr_proc) spinlock_acq(&proc->pg_lock);

    if (proc->proc_group->session->sid != curr_proc->proc_group->session->sid) {
        ret.err = EPERM;
        goto end;
    }

    ret.value = proc->proc_group->pgid;

end:
    if (proc != curr_proc) spinlock_rel(&proc->pg_lock);
    spinlock_rel(&curr_proc->pg_lock);
    proc_rel(proc);
    return ret;
}
