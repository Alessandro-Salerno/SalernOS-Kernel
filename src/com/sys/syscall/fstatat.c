#define _GNU_SOURCE
#include <arch/cpu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <stdatomic.h>
#include <stdio.h>

// SYSCALL: fstatat(int dir_fd, const struct syscall_str *path, struct stat
// *buf, int flags)
SYSCALL(syscall_fstatat) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(5);

    int dir_fd = SYSCALL_ARG(int, 1);
    const struct syscall_str *upath = SYSCALL_ARG(void *, 2);
    struct stat *buf = SYSCALL_ARG(void *, 3);
    int flags = SYSCALL_ARG(int, 4);

    char path[CONFIG_PATH_MAX];
    usize pathlen;
    int path_ret = syscall_copy_str(path, sizeof(path), upath, &pathlen);
    if (path_ret != 0) return SYSCALL_ERR(path_ret);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    Vnode *dir = NULL;
    int err = proc_get_directory(curr_proc, dir_fd, &dir);
    if (err != 0) return SYSCALL_ERR(err);

    Vnode *target = NULL;
    if ((flags & AT_EMPTY_PATH) && pathlen == 0) {
        target = dir;
        vfs_vnode_hold(target);
    } else {
        err = vfs_lookup(path, pathlen, curr_proc->root, dir,
                         (flags & AT_SYMLINK_NOFOLLOW) ? 0 : VFS_LOOKUP_FOLLOW,
                         &target);
    }

    struct stat kstat;
    if (err == 0) err = vfs_stat(target, false, &kstat);
    if (err == 0) err = memcpy_to_user(buf, &kstat, sizeof(kstat));

    vfs_vnode_rel(target);
    vfs_vnode_rel(dir);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
