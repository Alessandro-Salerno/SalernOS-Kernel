#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>

static bool proc_exists(pid_t pid) {
    Proc *proc = proc_get_by_pid(pid);
    if (proc == NULL) return false;

    // A reaped process stays in the table until its threads are reclaimed
    spinlock_acq(&proc->signal_lock);
    bool reaped = proc->state == PROC_STATE_EXITED && proc->state_notified;
    spinlock_rel(&proc->signal_lock);
    proc_rel(proc);
    return !reaped;
}

// SYSCALL: kill(pid_t pid, int sig)
SYSCALL(syscall_kill) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    pid_t pid = SYSCALL_ARG(pid_t, 1);
    int sig = SYSCALL_ARG(int, 2);

    SyscallRet ret = SYSCALL_BASE_OK();
    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    if (pid == -1) {
        // TODO: implement signal to all
        return SYSCALL_ERR(ENOSYS);
    }

    if (pid == 0) {
        spinlock_acq(&curr_proc->pg_lock);
        pid = -curr_proc->proc_group->pgid;
        spinlock_rel(&curr_proc->pg_lock);
    }

    // sig = 0 only checks that the target exists
    if (sig == 0) {
        bool exists = pid > 0 ? proc_exists(pid)
                              : proc_get_group_by_pgid(-pid) != NULL;
        if (!exists) ret.err = ESRCH;
        goto skip_send;
    }

    if (pid > 0) ret.err = signal_send_to_proc(pid, sig, curr_proc);
    else ret.err = signal_send_to_proc_group(-pid, sig, curr_proc);

skip_send:
    if (ret.err != 0) ret.value = -1;

    return ret;
}
