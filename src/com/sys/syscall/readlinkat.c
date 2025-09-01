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

// SYSCALL: readlinkat(int dir, const char *path, char *buf, size_t buflen)
COM_SYS_SYSCALL(com_sys_syscall_readlinkat) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();

    int         dir_fd = COM_SYS_SYSCALL_ARG(int, 1);
    const char *path   = COM_SYS_SYSCALL_ARG(void *, 2);
    char       *buf    = COM_SYS_SYSCALL_ARG(void *, 3);
    size_t      buflen = COM_SYS_SYSCALL_ARG(size_t, 4);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_syscall_ret_t ret = COM_SYS_SYSCALL_BASE_ERR();

    com_vnode_t *dir      = NULL;
    com_file_t  *dir_file = NULL;
    size_t       pathlen  = kstrlen(path);

    int dir_ret = com_sys_proc_get_directory(&dir_file,
                                             &dir,
                                             curr_proc,
                                             dir_fd);
    if (0 != dir_ret) {
        ret = COM_SYS_SYSCALL_ERR(dir_ret);
        goto end;
    }

    com_vnode_t *target  = NULL;
    int          vfs_err = com_fs_vfs_lookup(&target,
                                    path,
                                    pathlen,
                                    curr_proc->root,
                                    dir,
                                    false);

    if (0 != vfs_err) {
        ret = COM_SYS_SYSCALL_ERR(vfs_err);
        goto end;
    }

    if (E_COM_VNODE_TYPE_LINK != target->type) {
        ret = COM_SYS_SYSCALL_ERR(EINVAL);
        goto end;
    }

    const char *link    = NULL;
    size_t      linklen = 0;
    vfs_err             = com_fs_vfs_readlink(&link, &linklen, target);

    if (vfs_err) {
        ret = COM_SYS_SYSCALL_ERR(vfs_err);
        goto end;
    }

    size_t user_linklen = KMIN(linklen, buflen);
    kmemcpy(buf, link, user_linklen);

    ret = COM_SYS_SYSCALL_OK(user_linklen);

end:
    COM_FS_FILE_RELEASE(dir_file);
    COM_FS_VFS_VNODE_RELEASE(target);
    return ret;
}
