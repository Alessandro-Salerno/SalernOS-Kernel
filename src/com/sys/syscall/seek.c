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

// SYSCALL: seek(int fd, off_t offset, int whence)
SYSCALL(syscall_seek) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int fd = SYSCALL_ARG(int, 1);
    off_t offset = SYSCALL_ARG(off_t, 2);
    int whence = SYSCALL_ARG(int, 3);

    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        return SYSCALL_ERR(EINVAL);
    }

    SyscallRet ret = SYSCALL_BASE_OK();
    Proc *curr = cpu_get_thread()->proc;
    File *file = proc_get_file(curr, fd);

    if (file == NULL) return SYSCALL_ERR(EBADF);

    int seekable = vfs_seekable(file->vnode);
    if (seekable != 0) {
        FILE_REL(file);
        return SYSCALL_ERR(seekable);
    }

    off_t base = 0;
    if (whence == SEEK_END) {
        struct stat statbuf;
        int vfs_ret = vfs_stat(file->vnode, true, &statbuf);
        if (vfs_ret != 0) {
            ret = SYSCALL_ERR(vfs_ret);
            goto cleanup;
        }
        base = statbuf.st_size;
    }

    spinlock_acq(&file->off_lock);
    if (whence == SEEK_CUR) base = file->off;

    off_t new_off;
    int err = 0;
    if (__builtin_add_overflow(base, offset, &new_off)) err = EOVERFLOW;
    else if (new_off < 0) err = EINVAL;
    else file->off = new_off;
    spinlock_rel(&file->off_lock);

    ret = err != 0 ? SYSCALL_ERR(err) : SYSCALL_OK(new_off);

cleanup:
    assert(file != NULL);
    FILE_REL(file);
    return ret;
}
