#include <arch/cpu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <stdatomic.h>

// SYSCALL: faccessat(int dir_fd, const struct syscall_str *path, int mode,
// int flags)
SYSCALL(syscall_faccessat) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3); // TODO: implement mode and flags

    int dir_fd = SYSCALL_ARG(int, 1);
    const struct syscall_str *upath = SYSCALL_ARG(void *, 2);

    char path[CONFIG_PATH_MAX];
    usize pathlen;
    int path_ret = syscall_copy_str(path, sizeof(path), upath, &pathlen);
    if (path_ret != 0) return SYSCALL_ERR(path_ret);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    Vnode *dir = NULL;
    int err = proc_get_directory(curr_proc, dir_fd, &dir);
    if (err != 0) return SYSCALL_ERR(err);

    // POSIX says to always follow symlinks here, but Linux 5.8 apparently has
    // an faccessat2() that lets you disable this, so maybe some programs expect
    // it?
    Vnode *file_vn = NULL;
    err = vfs_lookup(path, pathlen, curr_proc->root, dir, VFS_LOOKUP_FOLLOW,
                     &file_vn);
    vfs_vnode_rel(file_vn);
    vfs_vnode_rel(dir);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
