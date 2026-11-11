#define _GNU_SOURCE
#include <arch/cpu.h>
#include <arch/info.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <stdatomic.h>
#include <stdio.h>

static SyscallRet fcntl_dup(Proc *curr_proc, int fd, int op, int arg1) {
    int new_fd = proc_duplicate_file_nolock(curr_proc, arg1, fd);

    if (new_fd < 0) return SYSCALL_ERR(-new_fd);

    if (op == F_DUPFD_CLOEXEC) {
        FileDesc *fildesc = proc_get_fildesc_nolock(curr_proc, new_fd);
        fildesc->flags |= FD_CLOEXEC;
    }

    return SYSCALL_OK(new_fd);
}

// SYSCALL: fcntl(fd, op, arg1)
SYSCALL(syscall_fcntl) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int fd = SYSCALL_ARG(int, 1);
    int op = SYSCALL_ARG(int, 2);
    int arg1 = SYSCALL_ARG(int, 3);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    SyscallRet ret = SYSCALL_BASE_OK();

    spinlock_acq(&curr_proc->fd_lock);
    FileDesc *fildes = proc_get_fildesc_nolock(curr_proc, fd);
    File *file = proc_get_file_nolock(curr_proc, fd);

    if (file == NULL || fildes == NULL) {
        ret = SYSCALL_ERR(EBADF);
        goto end;
    }

    if (op == F_GETFL) {
        ret.value = file->flags;
        goto end;
    }

    if (op == F_GETFD) {
        ret.value = fildes->flags;
        goto end;
    }

    if (op == F_SETFL) {
        file->flags = arg1;
        goto end;
    }

    if (op == F_SETFD) {
        fildes->flags = arg1;
        goto end;
    }

    if (op == F_DUPFD || op == F_DUPFD_CLOEXEC) {
        ret = fcntl_dup(curr_proc, fd, op, arg1);
        goto end;
    }

    if (op == F_GETPIPE_SZ) {
        ret = SYSCALL_OK(PAGE_SIZE);
        goto end;
    }

    ret = SYSCALL_ERR(ENOSYS);

end:
    spinlock_rel(&curr_proc->fd_lock);
    FILE_REL(file);
    return ret;
}
