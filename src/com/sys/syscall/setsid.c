#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALL: setsid(void)
SYSCALL(syscall_setsid) {
    SYSCALL_UNUSED_START(0);

    Proc *curr_proc = cpu_get_thread()->proc;
    spinlock_acq(&curr_proc->pg_lock);
    assert(curr_proc->proc_group != NULL);

    SyscallRet ret = SYSCALL_BASE_ERR();

    if (curr_proc->proc_group->pgid == curr_proc->pid) {
        ret.err = EPERM;
        goto end;
    }

    proc_new_session_nolock(curr_proc, NULL);
    ret.value = curr_proc->proc_group->pgid;

end:
    spinlock_rel(&curr_proc->pg_lock);
    return ret;
}
