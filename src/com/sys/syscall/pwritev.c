#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/ds/ioviter.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <sys/uio.h>

// SYSCALL: pwritev(int fd, struct iovec *iov, int iovcnt, off_t off)
SYSCALL(syscall_pwritev) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(5);

    int fd = SYSCALL_ARG(int, 1);
    struct iovec *iov = SYSCALL_ARG(void *, 2);
    int iovcnt = SYSCALL_ARG(int, 3);
    off_t off = SYSCALL_ARG(off_t, 4);

    if (off < 0) return SYSCALL_ERR(EINVAL);

    IOVIter ioviter;
    int iov_ret = ioviter_init_user(&ioviter, cpu_get_thread()->scratch, iov,
                                    iovcnt);
    if (iov_ret != 0) return SYSCALL_ERR(iov_ret);

    SyscallRet ret = SYSCALL_BASE_OK();

    Proc *curr = cpu_get_thread()->proc;
    File *file = proc_get_file(curr, fd);

    if (file == NULL) {
        ret = SYSCALL_ERR(EBADF);
        return ret;
    }

    int seekable = vfs_seekable(file->vnode);
    if (seekable != 0) {
        ret = SYSCALL_ERR(seekable);
        goto cleanup;
    }

    // Apparently Linux keeps O_APPEND here, so we do the same
    usize bytes_written = 0;
    int vfs_op = vfs_writev(file->vnode, &ioviter, off, file->flags,
                            &bytes_written);

    if (vfs_op != 0) {
        ret = SYSCALL_ERR(vfs_op);
        goto cleanup;
    }

    ret = SYSCALL_OK(bytes_written);
cleanup:
    FILE_REL(file);
    return ret;
}
