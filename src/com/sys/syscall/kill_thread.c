#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>

// SYSCALL: kill_thread(pid_t tid, int sig)
SYSCALL(syscall_kill_thread) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    pid_t tid = SYSCALL_ARG(pid_t, 1);
    int sig = SYSCALL_ARG(int, 2);

    SyscallRet ret = SYSCALL_BASE_OK();
    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    if (sig == 0) goto skip_send;

    spinlock_acq(&curr_proc->threads_lock);
    Thread *t, *_;
    bool found = false;
    TAILQ_FOREACH_SAFE(t, &curr_proc->threads, proc_threads, _) {
        if (t->tid == tid) {
            found = true;
            break;
        }
    }
    if (found) ret.err = signal_send_to_thread(t, sig, curr_proc);
    else ret.err = ESRCH;
    spinlock_rel(&curr_proc->threads_lock);

skip_send:
    if (ret.err != 0) ret.value = -1;

    return ret;
}
