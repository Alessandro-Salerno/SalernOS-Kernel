#include <arch/cpu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALL: linkat(int old_dir_fd, const struct syscall_str *oldpath,
// int new_dir_fd, const struct syscall_str *newpath, int flags)
SYSCALL(syscall_linkat) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(6);

    int old_dir_fd = SYSCALL_ARG(int, 1);
    const struct syscall_str *uoldpath = SYSCALL_ARG(void *, 2);
    int new_dir_fd = SYSCALL_ARG(int, 3);
    const struct syscall_str *unewpath = SYSCALL_ARG(void *, 4);
    int flags = SYSCALL_ARG(int, 5);

    if (flags & ~AT_SYMLINK_FOLLOW) return SYSCALL_ERR(EINVAL);

    char oldpath[CONFIG_PATH_MAX];
    usize oldpathlen;
    int oldpath_ret = syscall_copy_str(oldpath, sizeof(oldpath), uoldpath,
                                       &oldpathlen);
    if (oldpath_ret != 0) return SYSCALL_ERR(oldpath_ret);

    char newpath[CONFIG_PATH_MAX];
    usize newpathlen;
    int newpath_ret = syscall_copy_str(newpath, sizeof(newpath), unewpath,
                                       &newpathlen);
    if (newpath_ret != 0) return SYSCALL_ERR(newpath_ret);

    Proc *curr_proc = cpu_get_thread()->proc;
    Vnode *old_dir = NULL;
    Vnode *new_dir = NULL;
    Vnode *node = NULL;
    Vnode *parent = NULL;
    int err = proc_get_directory(curr_proc, old_dir_fd, &old_dir);
    if (err == 0) err = proc_get_directory(curr_proc, new_dir_fd, &new_dir);
    if (err != 0) goto end;

    err = vfs_lookup(oldpath, oldpathlen, curr_proc->root, old_dir,
                     (flags & AT_SYMLINK_FOLLOW) ? VFS_LOOKUP_FOLLOW : 0,
                     &node);
    if (err != 0) goto end;

    const char *name;
    usize namelen;
    bool must_dir;
    err = vfs_lookup_parent(newpath, newpathlen, curr_proc->root, new_dir,
                            &parent, &name, &namelen, &must_dir);
    if (err != 0) goto end;

    // A path that ends with '/' only names directories, which cannot be
    // linked, so it can only fail. POSIX.1-2017, 4.13 and link()
    if (must_dir && node->type != VNODE_TYPE_DIR) {
        Vnode *existing = NULL;
        err = vfs_lookup(name, namelen, parent, parent, 0, &existing);
        vfs_vnode_rel(existing);
        err = err == 0 ? EEXIST : ENOENT;
        goto end;
    }

    err = vfs_link(parent, name, namelen, node);

end:
    vfs_vnode_rel(parent);
    vfs_vnode_rel(node);
    vfs_vnode_rel(new_dir);
    vfs_vnode_rel(old_dir);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
