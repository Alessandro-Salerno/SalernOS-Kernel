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

#define _GNU_SOURCE
#include <arch/cpu.h>
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
#include <stdio.h>

// SYSCALL: fstatat(int dir_fd, const char *path, struct stat *buf, int flags)
COM_SYS_SYSCALL(com_sys_syscall_fstatat) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();

    int          dir_fd = COM_SYS_SYSCALL_ARG(int, 1);
    const char  *path   = COM_SYS_SYSCALL_ARG(void *, 2);
    struct stat *buf    = COM_SYS_SYSCALL_ARG(void *, 3);
    int          flags  = COM_SYS_SYSCALL_ARG(int, 4);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_syscall_ret_t ret = COM_SYS_SYSCALL_BASE_ERR();

    com_vnode_t *dir       = NULL;
    com_file_t  *dir_file  = NULL;
    bool         do_lookup = false;

    int dir_ret = com_sys_proc_get_directory(&dir_file,
                                             &dir,
                                             curr_proc,
                                             dir_fd);
    if (0 != dir_ret) {
        ret = COM_SYS_SYSCALL_ERR(dir_ret);
        goto end;
    }

    do_lookup            = !((AT_EMPTY_PATH & flags) && 0 == kstrlen(path));
    int          vfs_err = 0;
    com_vnode_t *target  = dir;

    if (do_lookup) {
        vfs_err = com_fs_vfs_lookup(&target,
                                    path,
                                    kstrlen(path),
                                    curr_proc->root,
                                    dir,
                                    (AT_SYMLINK_FOLLOW & flags) |
                                        !(AT_SYMLINK_NOFOLLOW & flags));
    }

    if (0 != vfs_err) {
        do_lookup = false;
        ret.err   = vfs_err;
        goto end;
    }

    vfs_err = com_fs_vfs_stat(buf, target);

    if (vfs_err) {
        ret.err = vfs_err;
        goto end;
    }

    ret.value = 0;

end:
    COM_FS_FILE_RELEASE(dir_file);
    if (do_lookup) {
        COM_FS_VFS_VNODE_RELEASE(target);
    }
    return ret;
}
