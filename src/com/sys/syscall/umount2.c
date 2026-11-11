#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

// SYSCALL: umount2(const struct syscall_str *target, int flags)
SYSCALL(syscall_umount2) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    const struct syscall_str *utarget = SYSCALL_ARG(void *, 1);
    int flags = SYSCALL_ARG(int, 2);

    // Neither forced nor lazy unmounts exist
    if (flags != 0) return SYSCALL_ERR(EINVAL);

    char path[CONFIG_PATH_MAX];
    usize pathlen;
    int err = syscall_copy_str(path, sizeof(path), utarget, &pathlen);
    if (err != 0) return SYSCALL_ERR(err);

    Proc *curr_proc = cpu_get_thread()->proc;
    Vnode *cwd = proc_get_cwd(curr_proc);
    Vnode *target = NULL;
    err = vfs_lookup(path, pathlen, curr_proc->root, cwd, VFS_LOOKUP_FOLLOW,
                     &target);
    vfs_vnode_rel(cwd);
    if (err == 0) {
        // The reference to target is consumed
        err = vfs_unmount(target);
    }

    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
