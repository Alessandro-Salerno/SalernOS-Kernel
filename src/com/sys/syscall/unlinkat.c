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

// SYSCALL: unlinkat(int dir_fd, const char *path, int flags)
COM_SYS_SYSCALL(com_sys_syscall_unlinkat) {
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

    int dir_ret =
        com_sys_proc_get_directory(&dir_file, &dir, curr_proc, dir_fd);
    if (0 != dir_ret) {
        ret = COM_SYS_SYSCALL_ERR(dir_ret);
        goto end;
    }

    int vfs_err = com_fs_vfs_unlink(dir, path, kstrlen(path), flags);
    if (0 != vfs_err) {
        ret = COM_SYS_SYSCALL_ERR(vfs_err);
        goto end;
    }

    ret = COM_SYS_SYSCALL_OK(0);

end:
    COM_FS_FILE_RELEASE(dir_file);
    return ret;
}
