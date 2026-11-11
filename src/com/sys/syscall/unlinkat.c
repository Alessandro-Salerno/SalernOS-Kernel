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

// SYSCALL: unlinkat(int dir_fd, const struct syscall_str *path, int flags)
SYSCALL(syscall_unlinkat) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int dir_fd = SYSCALL_ARG(int, 1);
    const struct syscall_str *upath = SYSCALL_ARG(void *, 2);
    int flags = SYSCALL_ARG(int, 3);

    char path[CONFIG_PATH_MAX];
    usize pathlen;
    int path_ret = syscall_copy_str(path, sizeof(path), upath, &pathlen);
    if (path_ret != 0) return SYSCALL_ERR(path_ret);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    Vnode *dir = NULL;
    int err = proc_get_directory(curr_proc, dir_fd, &dir);
    if (err != 0) return SYSCALL_ERR(err);

    Vnode *parent = NULL;
    const char *name;
    usize namelen;
    bool must_dir;
    err = vfs_lookup_parent(path, pathlen, curr_proc->root, dir, &parent, &name,
                            &namelen, &must_dir);
    vfs_vnode_rel(dir);
    if (err != 0) return SYSCALL_ERR(err);

    int vfs_flags = 0;
    if (flags & AT_REMOVEDIR) vfs_flags |= VFS_REMOVE_DIR;
    if (must_dir) vfs_flags |= VFS_MUST_DIR;

    err = vfs_unlink(parent, name, namelen, vfs_flags);
    vfs_vnode_rel(parent);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
