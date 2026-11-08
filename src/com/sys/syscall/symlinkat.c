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
#include <stdatomic.h>
#include <stdint.h>

// SYSCALL: symlinkat(const struct syscall_str *path1, int dir_fd,
// const struct syscall_str *path2)
COM_SYS_SYSCALL(com_sys_syscall_symlinkat) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    const struct syscall_str *upath1 = COM_SYS_SYSCALL_ARG(void *, 1);
    int                       dir_fd = COM_SYS_SYSCALL_ARG(int, 2);
    const struct syscall_str *upath2 = COM_SYS_SYSCALL_ARG(void *, 3);

    char   path1[CONFIG_PATH_MAX];
    size_t path1len;
    int    path1_ret = com_sys_syscall_copy_str(&path1len,
                                                path1,
                                                sizeof(path1),
                                                upath1);
    if (0 != path1_ret) {
        return COM_SYS_SYSCALL_ERR(path1_ret);
    }

    char   path2[CONFIG_PATH_MAX];
    size_t path2len;
    int    path2_ret = com_sys_syscall_copy_str(&path2len,
                                                path2,
                                                sizeof(path2),
                                                upath2);
    if (0 != path2_ret) {
        return COM_SYS_SYSCALL_ERR(path2_ret);
    }

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_vnode_t *dir = NULL;
    int          err = com_sys_proc_get_directory(&dir, curr_proc, dir_fd);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }

    com_vnode_attr_t attr = COM_FS_VFS_ATTR(E_COM_VNODE_TYPE_LINK);
    attr.target           = path1;
    attr.targetlen        = path1len;
    com_vnode_t *link     = NULL;
    err                   = com_fs_vfs_create_path(&link,
                                                   path2,
                                                   path2len,
                                                   curr_proc->root,
                                                   dir,
                                                   &attr,
                                                   COM_FS_VFS_CREATE_EXCL);
    COM_FS_VFS_VNODE_RELEASE(link);
    COM_FS_VFS_VNODE_RELEASE(dir);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }
    return COM_SYS_SYSCALL_OK(0);
}
