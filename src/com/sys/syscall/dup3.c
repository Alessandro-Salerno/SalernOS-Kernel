#include <arch/cpu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <stdatomic.h>

// SYSCALL: dup3(int old_fd, int new_fd, int fd_flags)
SYSCALL(syscall_dup3) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    int old_fd = SYSCALL_ARG(int, 1);
    int new_fd = SYSCALL_ARG(int, 2);
    // int flags  = SYSCALL_ARG(int, 3);
    // TODO: handle flags

    SyscallRet ret = SYSCALL_BASE_OK();
    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;
    File *file = NULL;

    spinlock_acq(&curr_proc->fd_lock);

    // POSIX.1-2017, dup2(): new_fd is only closed if old_fd is valid, and
    // nothing happens if they are equal
    FileDesc *old_desc = proc_get_fildesc_nolock(curr_proc, old_fd);
    if (old_desc == NULL || old_desc->file == NULL || new_fd < 0 ||
        new_fd >= CONFIG_OPEN_MAX) {
        ret = SYSCALL_ERR(EBADF);
    } else if (curr_proc->fd[new_fd].reserved) {
        // As Linux: new_fd is being allocated by another thread
        ret = SYSCALL_ERR(EBUSY);
    } else if (old_fd != new_fd) {
        proc_close_file_nolock(curr_proc, new_fd, &file);
        int dup_ret = proc_duplicate_file_nolock(curr_proc, new_fd, old_fd);
        if (dup_ret < 0) ret = SYSCALL_ERR(-dup_ret);
    }

    spinlock_rel(&curr_proc->fd_lock);
    FILE_REL(file);
    return ret;
}
