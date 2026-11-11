#include <arch/cpu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALL: renameat(int old_dir_fd, const struct syscall_str *oldpath,
// int new_dir_fd, const struct syscall_str *newpath)
SYSCALL(syscall_renameat) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(5);

    int old_dir_fd = SYSCALL_ARG(int, 1);
    const struct syscall_str *uoldpath = SYSCALL_ARG(void *, 2);
    int new_dir_fd = SYSCALL_ARG(int, 3);
    const struct syscall_str *unewpath = SYSCALL_ARG(void *, 4);

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
    Vnode *old_parent = NULL;
    Vnode *new_parent = NULL;
    int err = proc_get_directory(curr_proc, old_dir_fd, &old_dir);
    if (err == 0) err = proc_get_directory(curr_proc, new_dir_fd, &new_dir);
    if (err != 0) goto end;

    const char *old_name, *new_name;
    usize old_namelen, new_namelen;
    bool old_must_dir, new_must_dir;
    err = vfs_lookup_parent(oldpath, oldpathlen, curr_proc->root, old_dir,
                            &old_parent, &old_name, &old_namelen,
                            &old_must_dir);
    if (err != 0) goto end;

    err = vfs_lookup_parent(newpath, newpathlen, curr_proc->root, new_dir,
                            &new_parent, &new_name, &new_namelen,
                            &new_must_dir);
    if (err != 0) goto end;

    err = vfs_rename(old_parent, old_name, old_namelen, new_parent, new_name,
                     new_namelen,
                     (old_must_dir || new_must_dir) ? VFS_MUST_DIR : 0);

end:
    vfs_vnode_rel(new_parent);
    vfs_vnode_rel(old_parent);
    vfs_vnode_rel(new_dir);
    vfs_vnode_rel(old_dir);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
