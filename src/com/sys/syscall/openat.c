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

// SYSCALL: openat(int dir_fd, const struct syscall_str *path, int flags)
SYSCALL(syscall_openat) {
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
    int accmode = flags & O_ACCMODE;
    bool writable = accmode == O_WRONLY || accmode == O_RDWR;

    Vnode *dir = NULL;
    Vnode *file_vn = NULL;
    Vnode *open_vn = NULL;
    int fd = -1;
    int err = proc_get_directory(curr_proc, dir_fd, &dir);
    if (err != 0) goto end;

    int vfs_flags = (flags & O_NOFOLLOW) ? 0 : VFS_LOOKUP_FOLLOW;
    if (flags & O_CREAT) {
        if (flags & O_EXCL) vfs_flags |= VFS_CREATE_EXCL;
        err = vfs_create_path(path, pathlen, curr_proc->root, dir,
                              &VFS_ATTR(VNODE_TYPE_FILE), vfs_flags, &file_vn);
    } else {
        err = vfs_lookup(path, pathlen, curr_proc->root, dir, vfs_flags,
                         &file_vn);
    }
    if (err != 0) goto end;

    // POSIX.1-2017, open(): O_NOFOLLOW on a symbolic link fails with ELOOP, and
    // O_DIRECTORY on anything else than a directory with ENOTDIR
    if (file_vn->type == VNODE_TYPE_LINK) {
        err = ELOOP;
        goto end;
    }

    bool is_dir = file_vn->type == VNODE_TYPE_DIR;
    if ((flags & O_DIRECTORY) && !is_dir) {
        err = ENOTDIR;
        goto end;
    }

    if (is_dir && writable) {
        err = EISDIR;
        goto end;
    }

    if (writable && vfs_readonly(file_vn)) {
        err = EROFS;
        goto end;
    }

    if ((flags & O_TRUNC) && writable && file_vn->type == VNODE_TYPE_FILE) {
        err = vfs_truncate(file_vn, 0);
        if (err != 0) goto end;
    }

    // The vnode of the open file may be a new one (e.g., /dev/ptmx)
    err = vfs_open(file_vn, &open_vn);
    if (err != 0) goto end;

    // TODO: reserve this before creating or truncating. With a full table, open
    // fails with EMFILE after the file was changed (POSIX.1-2017, open())
    fd = proc_next_fd(curr_proc);
    if (fd == -1) {
        vfs_close(open_vn);
        vfs_vnode_rel(open_vn);
        err = EMFILE;
        goto end;
    }

    File *file = slab_alloc(sizeof(File));
    file->vnode = open_vn;
    file->flags = flags;
    file->num_ref = 1;
    file->off = 0;
    proc_install_fd(curr_proc, fd, file, 0);
    log_debug("returning fd=%d for %s", fd, path);

end:
    vfs_vnode_rel(file_vn);
    vfs_vnode_rel(dir);
    if (err != 0) return SYSCALL_ERR(err);
    return SYSCALL_OK(fd);
}
