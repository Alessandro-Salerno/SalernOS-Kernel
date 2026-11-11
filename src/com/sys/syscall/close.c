#include <arch/cpu.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <stdatomic.h>

// SYSCALL: close(int fd)
SYSCALL(syscall_close) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(2);

    int fd = SYSCALL_ARG(int, 1);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    int close_ret = proc_close_file(curr_proc, fd);

    if (close_ret != 0) return SYSCALL_ERR(close_ret);

    return SYSCALL_OK(0);
}
