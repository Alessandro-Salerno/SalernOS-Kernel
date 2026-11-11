#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <sys/mount.h>

#define MOUNT_TYPE_MAX 32

// SYSCALL: mount(const struct syscall_str *source,
// const struct syscall_str *target, const struct syscall_str *fstype,
// unsigned long flags, const void *data)
SYSCALL(syscall_mount) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(6);

    const struct syscall_str *usource = SYSCALL_ARG(void *, 1);
    const struct syscall_str *utarget = SYSCALL_ARG(void *, 2);
    const struct syscall_str *ufstype = SYSCALL_ARG(void *, 3);
    unsigned long flags = SYSCALL_ARG(unsigned long, 4);
    const void *data = SYSCALL_ARG(void *, 5);

    // No file system takes options yet
    if (data != NULL || (flags & ~(unsigned long)MS_RDONLY) != 0) {
        return SYSCALL_ERR(EINVAL);
    }

    char fstype[MOUNT_TYPE_MAX];
    usize fstypelen;
    int err = syscall_copy_str(fstype, sizeof(fstype), ufstype, &fstypelen);
    if (err == ENAMETOOLONG) return SYSCALL_ERR(ENODEV);
    if (err != 0) return SYSCALL_ERR(err);

    FSType *type = vfs_find_type(fstype, fstypelen);
    if (type == NULL) return SYSCALL_ERR(ENODEV);

    // The path buffer is reused for the target once the source is found
    char path[CONFIG_PATH_MAX];
    usize pathlen;
    Proc *curr_proc = cpu_get_thread()->proc;
    Vnode *cwd = proc_get_cwd(curr_proc);
    Vnode *source = NULL;
    Vnode *target = NULL;
    if (type->needs_backing) {
        err = syscall_copy_str(path, sizeof(path), usource, &pathlen);
        if (err != 0) goto end;

        err = vfs_lookup(path, pathlen, curr_proc->root, cwd, VFS_LOOKUP_FOLLOW,
                         &source);
        if (err != 0) goto end;
    }

    err = syscall_copy_str(path, sizeof(path), utarget, &pathlen);
    if (err != 0) goto end;

    err = vfs_lookup(path, pathlen, curr_proc->root, cwd, VFS_LOOKUP_FOLLOW,
                     &target);
    if (err != 0) goto end;

    Vfs *vfs = NULL;
    err = vfs_mount(type, source, target,
                    (flags & MS_RDONLY) ? VFS_MOUNT_RDONLY : 0, &vfs);

end:
    vfs_vnode_rel(target);
    vfs_vnode_rel(source);
    vfs_vnode_rel(cwd);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(0);
}
