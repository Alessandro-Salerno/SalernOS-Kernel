#include <arch/cpu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <time.h>

// SYSCALL: utimensat(int dir_fd, const struct syscall_str *path, const struct
// timespec times[2], int flags)
SYSCALL(syscall_utimensat) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(5);

    int dir_fd = SYSCALL_ARG(int, 1);
    const struct syscall_str *upath = SYSCALL_ARG(void *, 2);
    const struct timespec *utimes = SYSCALL_ARG(void *, 3);
    int flags = SYSCALL_ARG(int, 4);

    if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH))
        return SYSCALL_ERR(EINVAL);

    char path[CONFIG_PATH_MAX];
    usize pathlen;
    int err = syscall_copy_str(path, sizeof(path), upath, &pathlen);
    if (err != 0) return SYSCALL_ERR(err);

    struct timespec times[2];
    if (utimes != NULL) err = memcpy_from_user(times, utimes, sizeof(times));
    if (err != 0) return SYSCALL_ERR(err);

    Proc *curr_proc = cpu_get_thread()->proc;
    Vnode *dir = NULL;
    err = proc_get_directory(curr_proc, dir_fd, &dir);
    if (err != 0) return SYSCALL_ERR(err);

    // futimens comes here with AT_EMPTY_PATH and dir_fd being the file
    Vnode *target = NULL;
    if ((flags & AT_EMPTY_PATH) && pathlen == 0) {
        target = dir;
        vfs_vnode_hold(target);
    } else {
        err = vfs_lookup(path, pathlen, curr_proc->root, dir,
                         (flags & AT_SYMLINK_NOFOLLOW) ? 0 : VFS_LOOKUP_FOLLOW,
                         &target);
    }

    if (err == 0) err = vfs_set_times(target, utimes != NULL ? times : NULL);

    vfs_vnode_rel(target);
    vfs_vnode_rel(dir);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
