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
#include <lib/usercopy.h>
#include <lib/util.h>
#include <stdatomic.h>
#include <stdint.h>

// SYSCALL: readlinkat(int dir, const struct syscall_str *path, char *buf,
// size_t buflen)
COM_SYS_SYSCALL(com_sys_syscall_readlinkat) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(5);

    int                       dir_fd = COM_SYS_SYSCALL_ARG(int, 1);
    const struct syscall_str *upath  = COM_SYS_SYSCALL_ARG(void *, 2);
    char                     *buf    = COM_SYS_SYSCALL_ARG(void *, 3);
    size_t                    buflen = COM_SYS_SYSCALL_ARG(size_t, 4);

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

    com_vnode_t *dir = NULL;
    int          err = com_sys_proc_get_directory(&dir, curr_proc, dir_fd);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }

    com_vnode_t *target = NULL;
    err = com_fs_vfs_lookup(&target, path, pathlen, curr_proc->root, dir, 0);
    COM_FS_VFS_VNODE_RELEASE(dir);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }

    // The path is not needed anymore, and symbolic links are shorter than
    // CONFIG_PATH_MAX (see com_fs_vfs_create)
    size_t linklen = 0;
    err            = com_fs_vfs_readlink(&linklen, path, sizeof(path), target);
    COM_FS_VFS_VNODE_RELEASE(target);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }

    size_t user_linklen = KMIN(KMIN(linklen, sizeof(path)), buflen);
    err                 = kmemcpy_to_user(buf, path, user_linklen);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }

    return COM_SYS_SYSCALL_OK(user_linklen);
}
