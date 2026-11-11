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

// TODO: implement file system mod
// SYSCALL: mkdirat(int dir_fd, const struct syscall_str *path, mode_t mode)
SYSCALL(syscall_mkdirat) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    int dir_fd = SYSCALL_ARG(int, 1);
    const struct syscall_str *upath = SYSCALL_ARG(void *, 2);
    mode_t mode = SYSCALL_ARG(mode_t, 3);
    (void)mode;

    char path[CONFIG_PATH_MAX];
    usize pathlen;
    int path_ret = syscall_copy_str(path, sizeof(path), upath, &pathlen);
    if (path_ret != 0) return SYSCALL_ERR(path_ret);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    Vnode *dir = NULL;
    int err = proc_get_directory(curr_proc, dir_fd, &dir);
    if (err != 0) return SYSCALL_ERR(err);

    Vnode *new_dir = NULL;
    err = vfs_create_path(path, pathlen, curr_proc->root, dir,
                          &VFS_ATTR(VNODE_TYPE_DIR), VFS_CREATE_EXCL, &new_dir);
    vfs_vnode_rel(new_dir);
    vfs_vnode_rel(dir);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
