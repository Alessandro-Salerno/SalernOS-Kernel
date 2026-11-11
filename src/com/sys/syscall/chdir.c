#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <stdatomic.h>

// SYSCALL: chdir(const struct syscall_str *path)
SYSCALL(syscall_chdir) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(2);

    const struct syscall_str *upath = SYSCALL_ARG(void *, 1);

    char path[CONFIG_PATH_MAX];
    usize pathlen;
    int path_ret = syscall_copy_str(path, sizeof(path), upath, &pathlen);
    if (path_ret != 0) return SYSCALL_ERR(path_ret);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    Vnode *cwd = proc_get_cwd(curr_proc);
    Vnode *new_cwd = NULL;
    int err = vfs_lookup(path, pathlen, curr_proc->root, cwd, VFS_LOOKUP_FOLLOW,
                         &new_cwd);
    vfs_vnode_rel(cwd);
    if (err != 0) return SYSCALL_ERR(err);

    if (new_cwd->type != VNODE_TYPE_DIR) {
        vfs_vnode_rel(new_cwd);
        return SYSCALL_ERR(ENOTDIR);
    }

    proc_set_cwd(curr_proc, new_cwd);
    vfs_vnode_rel(new_cwd);
    return SYSCALL_OK(0);
}
