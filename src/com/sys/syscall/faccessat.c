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

// SYSCALL: faccessat(int dir_fd, const struct syscall_str *path, int mode,
// int flags)
COM_SYS_SYSCALL(com_sys_syscall_faccessat) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3); // TODO: implement mode and flags

    int                       dir_fd = COM_SYS_SYSCALL_ARG(int, 1);
    const struct syscall_str *upath  = COM_SYS_SYSCALL_ARG(void *, 2);

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

    // POSIX says to always follow symlinks here, but Linux 5.8 apparently has
    // an faccessat2() that lets you disable this, so maybe some programs expect
    // it?
    com_vnode_t *file_vn = NULL;
    err                  = com_fs_vfs_lookup(&file_vn,
                                             path,
                                             pathlen,
                                             curr_proc->root,
                                             dir,
                                             COM_FS_VFS_LOOKUP_FOLLOW);
    COM_FS_VFS_VNODE_RELEASE(file_vn);
    COM_FS_VFS_VNODE_RELEASE(dir);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }
    return COM_SYS_SYSCALL_OK(0);
}
