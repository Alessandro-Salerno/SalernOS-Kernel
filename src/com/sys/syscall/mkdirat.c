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

// TODO: implement file system mod
// SYSCALL: mkdirat(int dir_fd, const struct syscall_str *path, mode_t mode)
COM_SYS_SYSCALL(com_sys_syscall_mkdirat) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int                       dir_fd = COM_SYS_SYSCALL_ARG(int, 1);
    const struct syscall_str *upath  = COM_SYS_SYSCALL_ARG(void *, 2);
    mode_t                    mode   = COM_SYS_SYSCALL_ARG(mode_t, 3);
    (void)mode;

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

    com_vnode_t *new_dir = NULL;
    err = com_fs_vfs_create_path(&new_dir,
                                 path,
                                 pathlen,
                                 curr_proc->root,
                                 dir,
                                 &COM_FS_VFS_ATTR(E_COM_VNODE_TYPE_DIR),
                                 COM_FS_VFS_CREATE_EXCL);
    COM_FS_VFS_VNODE_RELEASE(new_dir);
    COM_FS_VFS_VNODE_RELEASE(dir);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }
    return COM_SYS_SYSCALL_OK(0);
}
