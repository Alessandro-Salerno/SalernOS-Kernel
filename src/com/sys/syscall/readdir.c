#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALL: readdir(int fd, void *buffer, usize buflen)
SYSCALL(syscall_readdir) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int fd = SYSCALL_ARG(int, 1);
    void *buf = SYSCALL_ARG(void *, 2);
    usize buflen = SYSCALL_ARG(usize, 3);

    if (!MMU_RANGE_IS_USER(buf, buflen)) return SYSCALL_ERR(EFAULT);

    SyscallRet ret = SYSCALL_BASE_ERR();

    Proc *curr_proc = cpu_get_thread()->proc;
    File *file = proc_get_file(curr_proc, fd);

    if (file == NULL) {
        ret.err = EBADF;
        return ret;
    }

    // The offset is an opaque cookie
    spinlock_acq(&file->off_lock);
    u64 off = file->off;
    spinlock_rel(&file->off_lock);

    usize bytes_read = 0;
    u64 next_off = off;
    int vfs_op = vfs_readdir(buf, buflen, file->vnode, off, &bytes_read,
                             &next_off);

    if (vfs_op != 0) {
        ret.err = vfs_op;
        goto cleanup;
    }

    spinlock_acq(&file->off_lock);
    file->off = next_off;
    spinlock_rel(&file->off_lock);

    ret.value = bytes_read;
cleanup:
    FILE_REL(file);
    return ret;
}
