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

// SYSCALL: readlinkat(int dir, const struct syscall_str *path, char *buf,
// usize buflen)
SYSCALL(syscall_readlinkat) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(5);

    int dir_fd = SYSCALL_ARG(int, 1);
    const struct syscall_str *upath = SYSCALL_ARG(void *, 2);
    char *buf = SYSCALL_ARG(void *, 3);
    usize buflen = SYSCALL_ARG(usize, 4);

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
    err = vfs_lookup(path, pathlen, curr_proc->root, dir, 0, &target);
    vfs_vnode_rel(dir);
    if (err != 0) return SYSCALL_ERR(err);

    // The path is not needed anymore, and symbolic links are shorter than
    // CONFIG_PATH_MAX (see vfs_create)
    usize linklen = 0;
    err = vfs_readlink(path, sizeof(path), target, &linklen);
    vfs_vnode_rel(target);
    if (err != 0) return SYSCALL_ERR(err);

    usize user_linklen = min(min(linklen, sizeof(path)), buflen);
    err = memcpy_to_user(buf, path, user_linklen);
    if (err != 0) return SYSCALL_ERR(err);

    return SYSCALL_OK(user_linklen);
}
