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
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

// SYSCALL: truncate(int fd, off_t size)
COM_SYS_SYSCALL(com_sys_syscall_truncate) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    int   fd   = COM_SYS_SYSCALL_ARG(int, 1);
    off_t size = COM_SYS_SYSCALL_ARG(off_t, 2);

    com_syscall_ret_t ret  = COM_SYS_SYSCALL_BASE_OK();
    com_proc_t       *curr = ARCH_CPU_GET_THREAD()->proc;
    com_file_t       *file = com_sys_proc_get_file(curr, fd);

    if (NULL == file) {
        return COM_SYS_SYSCALL_ERR(EBADF);
    }

    int vfs_ret = com_fs_vfs_truncate(file->vnode, size);

    if (vfs_ret < 0) {
        ret = COM_SYS_SYSCALL_ERR(vfs_ret);
    }

    COM_FS_FILE_RELEASE(file);
    return ret;
}
