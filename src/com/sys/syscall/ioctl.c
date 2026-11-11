#include <arch/cpu.h>
#include <asm/ioctls.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

#define FDIOCTL_OK(n) \
    (struct FDIOCtlRet) { .ret = SYSCALL_OK(n), .done = true }
#define FDIOCTL_ERR(errno) \
    (struct FDIOCtlRet) { .ret = SYSCALL_ERR(errno), .done = true }
#define FDIOCTL_NOP() \
    (struct FDIOCtlRet) { .done = false }

struct FDIOCtlRet {
    SyscallRet ret;
    bool done;
};

// For ioctls that some unholy creature decided should be ioctls and not fcntls
static struct FDIOCtlRet fd_ioctl(int fd, u64 op, void *buf, Proc *proc) {
    (void)buf; // temporary probably
    spinlock_acq(&proc->fd_lock);
    FileDesc *fildesc = proc_get_fildesc_nolock(proc, fd);
    if (fildesc == NULL) {
        spinlock_rel(&proc->fd_lock);
        return FDIOCTL_ERR(EBADF);
    }

    struct FDIOCtlRet ret = FDIOCTL_NOP();

    if (op == FIOCLEX) {
        fildesc->flags |= FD_CLOEXEC;
        ret = FDIOCTL_OK(0);
    }

    spinlock_rel(&proc->fd_lock);
    return ret;
}

// SYSCALL: ioctl(int fd, int op, void *buf)
SYSCALL(syscall_ioctl) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int fd = SYSCALL_ARG(int, 1);
    u64 op = SYSCALL_ARG(u64, 2);
    void *buf = SYSCALL_ARG(void *, 3);

    SyscallRet ret = SYSCALL_BASE_OK();
    Proc *curr_proc = cpu_get_thread()->proc;

    struct FDIOCtlRet fdioctl_ret = fd_ioctl(fd, op, buf, curr_proc);
    if (fdioctl_ret.done) return fdioctl_ret.ret;

    File *file = proc_get_file(curr_proc, fd);
    if (file == NULL) {
        ret = SYSCALL_ERR(EBADF);
        goto end;
    }

    int vfs_op = vfs_ioctl(file->vnode, op, buf);

    if (vfs_op != 0) {
        ret = SYSCALL_ERR(vfs_op);
        goto end;
    }

end:
    FILE_REL(file);
    return ret;
}
