#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALL: setpgid(pid_t pid, pid_t pgid)
SYSCALL(syscall_setpgid) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    pid_t pid = SYSCALL_ARG(pid_t, 1);
    pid_t pgid = SYSCALL_ARG(pid_t, 2);

    if (pgid < 0 || pid < 0) return SYSCALL_ERR(EINVAL);

    SyscallRet ret = SYSCALL_BASE_ERR();
    Proc *curr_proc = cpu_get_thread()->proc;

    Proc *proc = curr_proc;
    if (pid != 0 && pid != curr_proc->pid) {
        proc = proc_get_by_pid(pid);
        if (proc == NULL) return SYSCALL_ERR(ESRCH);
        if (proc->parent_pid != curr_proc->pid) {
            proc_rel(proc);
            return SYSCALL_ERR(ESRCH);
        }
    }

    spinlock_acq(&curr_proc->pg_lock);
    ProcSession *session = curr_proc->proc_group->session;
    spinlock_rel(&curr_proc->pg_lock);
    spinlock_acq(&proc->pg_lock);
    assert(proc->proc_group != NULL);

    if (pgid == 0) pgid = proc->pid;

    if (proc != curr_proc && proc->did_execve) {
        ret.err = EACCES;
        goto end;
    }

    assert(proc->proc_group->session != NULL);
    if (proc->proc_group->session != session || proc->pid == session->sid) {
        ret.err = EPERM;
        goto end;
    }

    if (pgid == proc->proc_group->pgid) {
        ret.value = 0;
        goto end;
    }

    // Doing a query on the process group hashmap is actually thread safe even
    // if the lock is acquired only inside the function. This is because we know
    // (hopefully) that a process group may only be created by setpgid and
    // setsid, which first need to acquire the locks above (proc->pg_lock)
    ProcGroup *group = proc_get_group_by_pgid(pgid);
    if (group == NULL) {
        if (pgid != proc->pid) {
            ret.err = EPERM;
            goto end;
        }
        group = proc_new_group(proc, NULL);
    } else if (group->session != session) {
        ret.err = EPERM;
        goto end;
    }
    proc_join_group_nolock(proc, group);

    ret.value = 0;

end:
    spinlock_rel(&proc->pg_lock);
    if (proc != curr_proc) proc_rel(proc);
    return ret;
}
