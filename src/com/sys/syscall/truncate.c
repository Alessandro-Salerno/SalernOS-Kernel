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
#include <stdio.h>

// SYSCALL: truncate(int fd, off_t size)
SYSCALL(syscall_truncate) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    int fd = SYSCALL_ARG(int, 1);
    off_t size = SYSCALL_ARG(off_t, 2);

    if (size < 0) return SYSCALL_ERR(EINVAL);

    Proc *curr = cpu_get_thread()->proc;
    File *file = proc_get_file(curr, fd);
    if (file == NULL) return SYSCALL_ERR(EBADF);

    // POSIX.1-2017, ftruncate(): EINVAL if the file is not open for writing
    int accmode = file->flags & O_ACCMODE;
    int err = EINVAL;
    if (accmode == O_WRONLY || accmode == O_RDWR) {
        err = vfs_truncate(file->vnode, size);
    }

    FILE_REL(file);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
