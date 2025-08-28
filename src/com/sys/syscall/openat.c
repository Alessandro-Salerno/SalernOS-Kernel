/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
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
#include <stdatomic.h>
#include <stdint.h>

// SYSCALL: openat(int dir_fd, const char *path, int flags)
COM_SYS_SYSCALL(com_sys_syscall_openat) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int         dir_fd = COM_SYS_SYSCALL_ARG(int, 1);
    const char *path   = COM_SYS_SYSCALL_ARG(void *, 2);
    int         flags  = COM_SYS_SYSCALL_ARG(int, 3);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_syscall_ret_t ret = COM_SYS_SYSCALL_BASE_ERR();

    com_vnode_t *dir      = NULL;
    com_file_t  *dir_file = NULL;
    size_t       pathlen  = kstrlen(path);

    int dir_ret =
        com_sys_proc_get_directory(&dir_file, &dir, curr_proc, dir_fd);
    if (0 != dir_ret) {
        ret = COM_SYS_SYSCALL_ERR(dir_ret);
        goto end;
    }

    int          vfs_err = 0;
    com_vnode_t *file_vn = NULL;

    // TODO: race condition here. File could be deleted or created while we look
    // it up
    if (O_CREAT & flags) {
        com_vnode_t *existant = NULL;
        vfs_err               = com_fs_vfs_lookup(&existant,
                                    path,
                                    pathlen,
                                    curr_proc->root,
                                    dir,
                                    !(O_NOFOLLOW & flags));

        if ((O_EXCL & flags) && 0 == vfs_err) {
            COM_FS_VFS_VNODE_RELEASE(existant);
            ret.err = EEXIST;
            goto end;
        }

        if (NULL != existant) {
            KASSERT(0 == vfs_err);
            file_vn = existant;
            goto setup_fd;
        }

        // if NULL == existant
        size_t penult_len, end_idx, end_len;
        kstrpathpenult(path, pathlen, &penult_len, &end_idx, &end_len);

        vfs_err = com_fs_vfs_lookup(&dir,
                                    path,
                                    penult_len,
                                    curr_proc->root,
                                    dir,
                                    !(O_NOFOLLOW & flags));

        if (0 != vfs_err) {
            ret = COM_SYS_SYSCALL_ERR(vfs_err);
            goto end;
        }

        KASSERT(NULL != dir);
        com_fs_vfs_create(&file_vn, dir, &path[end_idx], end_len, 0);
        COM_FS_VFS_VNODE_RELEASE(dir);
        goto setup_fd;
    }

    // if no O_CREAT
    vfs_err =
        com_fs_vfs_lookup(&file_vn, path, pathlen, curr_proc->root, dir, true);
    if (0 != vfs_err) {
        ret = COM_SYS_SYSCALL_ERR(vfs_err);
        goto end;
    }

setup_fd:
    KASSERT(NULL != file_vn);

    com_vnode_t *new_vn   = NULL;
    int          open_ret = com_fs_vfs_open(&new_vn, file_vn);
    if (0 != open_ret) {
        COM_FS_VFS_VNODE_RELEASE(file_vn);
        ret = COM_SYS_SYSCALL_ERR(open_ret);
        goto end;
    }

    // If the open call has spawned a new vnode (as is the case, for example,
    // for /dev/ptmx)
    if (new_vn != file_vn && NULL != new_vn) {
        COM_FS_VFS_VNODE_RELEASE(file_vn);
        file_vn = new_vn;
    }

    if ((O_DIRECTORY & flags) && E_COM_VNODE_TYPE_DIR != file_vn->type) {
        COM_FS_VFS_VNODE_RELEASE(file_vn);
        ret = COM_SYS_SYSCALL_ERR(ENOTDIR);
        goto end;
    }

    int fd = com_sys_proc_next_fd(curr_proc);
    if (-1 == fd) {
        ret.err = EMFILE;
        goto end;
    }

    curr_proc->fd[fd].file = com_mm_slab_alloc(sizeof(com_file_t));
    com_file_t *file       = curr_proc->fd[fd].file;
    file->vnode            = file_vn;
    file->flags            = flags;
    file->num_ref          = 1;
    file->off              = 0;
    ret.value              = fd;
    KDEBUG("returning fd=%d (file=%p, vn=%p) for %s", fd, file, file_vn, path);

end:
    COM_FS_FILE_RELEASE(dir_file);
    return ret;
}
