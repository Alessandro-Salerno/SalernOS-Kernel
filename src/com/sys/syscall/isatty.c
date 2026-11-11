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

// SYSCALL: isatty(int fd)
SYSCALL(syscall_isatty) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(2);

    int fd = SYSCALL_ARG(int, 1);

    Proc *curr = cpu_get_thread()->proc;
    File *file = proc_get_file(curr, fd);
    SyscallRet ret = SYSCALL_BASE_OK();

    if (file == NULL) {
        log_debug("isatty(%d) failed", fd);
        return SYSCALL_ERR(EBADF);
    }

    int vfs_ret = vfs_isatty(file->vnode);

    if (vfs_ret != 0) {
        log_debug("isatty(%d) failed", fd);
        ret.value = -1;
        ret.err = vfs_ret;
        goto cleanup;
    }

    ret.value = 1;

cleanup:
    FILE_REL(file);
    return ret;
}
