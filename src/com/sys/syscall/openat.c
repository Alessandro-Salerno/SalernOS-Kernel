/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#include <arch/cpu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/spinlock.h>
#include <lib/str.h>
#include <lib/util.h>
#include <stdbool.h>
#include <stdint.h>

// SYSCALL: openat(int dir_fd, const struct syscall_str *path, int flags)
COM_SYS_SYSCALL(com_sys_syscall_openat) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int                       dir_fd = COM_SYS_SYSCALL_ARG(int, 1);
    const struct syscall_str *upath  = COM_SYS_SYSCALL_ARG(void *, 2);
    int                       flags  = COM_SYS_SYSCALL_ARG(int, 3);

    char   path[CONFIG_PATH_MAX];
    size_t pathlen;
    int    path_ret = com_sys_syscall_copy_str(&pathlen,
                                               path,
                                               sizeof(path),
                                               upath);
    if (0 != path_ret) {
        return COM_SYS_SYSCALL_ERR(path_ret);
    }

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;
    int           accmode     = O_ACCMODE & flags;
    bool          writable    = O_WRONLY == accmode || O_RDWR == accmode;

    com_vnode_t *dir     = NULL;
    com_vnode_t *file_vn = NULL;
    com_vnode_t *open_vn = NULL;
    int          fd      = -1;
    int          err     = com_sys_proc_get_directory(&dir, curr_proc, dir_fd);
    if (0 != err) {
        goto end;
    }

    int vfs_flags = (O_NOFOLLOW & flags) ? 0 : COM_FS_VFS_LOOKUP_FOLLOW;
    if (O_CREAT & flags) {
        if (O_EXCL & flags) {
            vfs_flags |= COM_FS_VFS_CREATE_EXCL;
        }
        err = com_fs_vfs_create_path(&file_vn,
                                     path,
                                     pathlen,
                                     curr_proc->root,
                                     dir,
                                     &COM_FS_VFS_ATTR(E_COM_VNODE_TYPE_FILE),
                                     vfs_flags);
    } else {
        err = com_fs_vfs_lookup(&file_vn,
                                path,
                                pathlen,
                                curr_proc->root,
                                dir,
                                vfs_flags);
    }
    if (0 != err) {
        goto end;
    }

    // POSIX.1-2017, open(): O_NOFOLLOW on a symbolic link fails with ELOOP, and
    // O_DIRECTORY on anything else than a directory with ENOTDIR
    if (E_COM_VNODE_TYPE_LINK == file_vn->type) {
        err = ELOOP;
        goto end;
    }

    bool is_dir = E_COM_VNODE_TYPE_DIR == file_vn->type;
    if ((O_DIRECTORY & flags) && !is_dir) {
        err = ENOTDIR;
        goto end;
    }

    if (is_dir && writable) {
        err = EISDIR;
        goto end;
    }

    if (writable && com_fs_vfs_readonly(file_vn)) {
        err = EROFS;
        goto end;
    }

    if ((O_TRUNC & flags) && writable &&
        E_COM_VNODE_TYPE_FILE == file_vn->type) {
        err = com_fs_vfs_truncate(file_vn, 0);
        if (0 != err) {
            goto end;
        }
    }

    // The vnode of the open file may be a new one (e.g., /dev/ptmx)
    err = com_fs_vfs_open(&open_vn, file_vn);
    if (0 != err) {
        goto end;
    }

    // TODO: reserve this before creating or truncating. With a full table, open
    // fails with EMFILE after the file was changed (POSIX.1-2017, open())
    fd = com_sys_proc_next_fd(curr_proc);
    if (-1 == fd) {
        com_fs_vfs_close(open_vn);
        COM_FS_VFS_VNODE_RELEASE(open_vn);
        err = EMFILE;
        goto end;
    }

    com_file_t *file = com_mm_slab_alloc(sizeof(com_file_t));
    file->vnode      = open_vn;
    file->flags      = flags;
    file->num_ref    = 1;
    file->off        = 0;
    com_sys_proc_install_fd(curr_proc, fd, file, 0);
    KDEBUG("returning fd=%d for %s", fd, path);

end:
    COM_FS_VFS_VNODE_RELEASE(file_vn);
    COM_FS_VFS_VNODE_RELEASE(dir);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }
    return COM_SYS_SYSCALL_OK(fd);
}
