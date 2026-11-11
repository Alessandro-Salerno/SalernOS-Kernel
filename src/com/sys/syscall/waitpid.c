#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <stdatomic.h>
#include <sys/wait.h>

static int waitpid_check(Proc *curr_proc, Proc *towait, int *out_status,
                         bool *out_reaped) {
    spinlock_acq(&towait->signal_lock);

    if (towait->state == PROC_STATE_EXITED && towait->state_notified) {
        spinlock_rel(&towait->signal_lock);
        return -ECHILD;
    }

    if (towait->state == PROC_STATE_EXITED) {
        *out_status = towait->state_cause_sig == SIGNAL_NONE
            ? (towait->exit_code & 0xff) << 8
            : towait->state_cause_sig & 0x7f;
        towait->state_notified = true;
        __atomic_sub_fetch(&curr_proc->num_children, 1, __ATOMIC_RELAXED);
        spinlock_rel(&towait->signal_lock);
        *out_reaped = true;
        return towait->pid;
    }

    if (towait->state == PROC_STATE_STOPPED && !towait->state_notified) {
        *out_status = (towait->state_cause_sig << 8) | 0x7F;
        towait->state_notified = true;
        spinlock_rel(&towait->signal_lock);
        return towait->pid;
    }

    spinlock_rel(&towait->signal_lock);
    return 0;
}

static int waitpid_proc(Proc *curr_proc, pid_t pid, int flags,
                        int *out_status) {
    while (true) {
        spinlock_acq(&curr_proc->waitpid_lock);

        Proc *towait = pid == -1 ? proc_get_arbitrary_child(curr_proc)
                                 : proc_get_by_pid(pid);

        int ret = -ECHILD;
        bool reaped = false;
        bool slept = false;
        if (towait != NULL &&
            __atomic_load_n(&curr_proc->num_children, __ATOMIC_RELAXED) != 0 &&
            towait->parent_pid == curr_proc->pid) {
            ret = waitpid_check(curr_proc, towait, out_status, &reaped);
        }

        if (ret == 0 && !(flags & WNOHANG)) {
            ret = -spinlock_wait(&curr_proc->waitpid_lock,
                                 &curr_proc->waitpid_waitlist, 0, true);
            slept = true;
        }

        spinlock_rel(&curr_proc->waitpid_lock);

        if (towait != NULL) {
            if (reaped) proc_rel(towait);
            proc_rel(towait);
        }

        if (!slept || ret != 0) return ret;
    }
}

static int waitpid_group(Proc *curr_proc, int pgid, int flags) {
    // TODO: implement this
    (void)curr_proc;
    (void)pgid;
    (void)flags;
    return -ENOSYS;
}

// SYSCALL: waitpid(pid_t pid, int *status, int flags)
SYSCALL(syscall_waitpid) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    pid_t pid = SYSCALL_ARG(pid_t, 1);
    int *status = SYSCALL_ARG(int *, 2);
    int flags = SYSCALL_ARG(int, 3);

    Proc *curr_proc = cpu_get_thread()->proc;
    int ret = 0; // 0 is NOT valid

    if (pid < -1) {
        ret = waitpid_group(curr_proc, -pid, flags);
        goto end;
    }

    int kstatus = 0;
    ret = waitpid_proc(curr_proc, pid, flags, &kstatus);
    if (ret > 0 && status != NULL &&
        memcpy_to_user(status, &kstatus, sizeof(kstatus)) != 0) {
        ret = -EFAULT;
    }

end:
    if (ret < 0) return SYSCALL_ERR(-ret);

    return SYSCALL_OK(ret);
}
