#include <arch/cpu.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <stdatomic.h>
#include <vendor/tailq.h>

// SYSCALL: exit_thread()
SYSCALL(syscall_exit_thread) {
    SYSCALL_UNUSED_START(0);

    Thread *curr_thread = cpu_get_thread();

    assert(__atomic_load_n(&curr_thread->state, __ATOMIC_RELAXED) ==
           THREAD_STATE_RUNNING);
    spinlock_acq(&curr_thread->proc->threads_lock);
    __atomic_store_n(&curr_thread->exit_requested, true, __ATOMIC_RELEASE);
    bool last = proc_all_threads_exiting_nolock(curr_thread->proc);
    spinlock_rel(&curr_thread->proc->threads_lock);

    if (last) {
        // Other exiting threads may still be switching out. Join their
        // acknowledgements before releasing shared process resources
        proc_terminate(0, SIGNAL_NONE);
    }

    thread_exit(curr_thread);
    __builtin_unreachable();
}
