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

// SYSCALL: symlinkat(const struct syscall_str *path1, int dir_fd,
// const struct syscall_str *path2)
SYSCALL(syscall_symlinkat) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(4);

    const struct syscall_str *upath1 = SYSCALL_ARG(void *, 1);
    int dir_fd = SYSCALL_ARG(int, 2);
    const struct syscall_str *upath2 = SYSCALL_ARG(void *, 3);

    char path1[CONFIG_PATH_MAX];
    usize path1len;
    int path1_ret = syscall_copy_str(path1, sizeof(path1), upath1, &path1len);
    if (path1_ret != 0) return SYSCALL_ERR(path1_ret);

    char path2[CONFIG_PATH_MAX];
    usize path2len;
    int path2_ret = syscall_copy_str(path2, sizeof(path2), upath2, &path2len);
    if (path2_ret != 0) return SYSCALL_ERR(path2_ret);

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    Vnode *dir = NULL;
    int err = proc_get_directory(curr_proc, dir_fd, &dir);
    if (err != 0) return SYSCALL_ERR(err);

    VnodeAttr attr = VFS_ATTR(VNODE_TYPE_LINK);
    attr.target = path1;
    attr.targetlen = path1len;
    Vnode *link = NULL;
    err = vfs_create_path(path2, path2len, curr_proc->root, dir, &attr,
                          VFS_CREATE_EXCL, &link);
    vfs_vnode_rel(link);
    vfs_vnode_rel(dir);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
