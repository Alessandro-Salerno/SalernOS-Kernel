#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>

// SYSCALL: chroot(const struct syscall_str *path)
SYSCALL(syscall_chroot) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(2);

    const struct syscall_str *upath = SYSCALL_ARG(void *, 1);

    char path[CONFIG_PATH_MAX];
    usize pathlen;
    int err = syscall_copy_str(path, sizeof(path), upath, &pathlen);
    if (err != 0) return SYSCALL_ERR(err);

    Proc *curr_proc = cpu_get_thread()->proc;
    Vnode *root = proc_get_root(curr_proc);
    Vnode *cwd = proc_get_cwd(curr_proc);
    Vnode *new_root = NULL;
    err = vfs_lookup(path, pathlen, root, cwd, VFS_LOOKUP_FOLLOW, &new_root);
    vfs_vnode_rel(cwd);
    vfs_vnode_rel(root);
    if (err != 0) return SYSCALL_ERR(err);

    if (new_root->type != VNODE_TYPE_DIR) {
        vfs_vnode_rel(new_root);
        return SYSCALL_ERR(ENOTDIR);
    }

    proc_set_root(curr_proc, new_root);
    vfs_vnode_rel(new_root);
    return SYSCALL_OK(0);
}
