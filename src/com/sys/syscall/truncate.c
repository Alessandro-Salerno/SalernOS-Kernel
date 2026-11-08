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
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

// SYSCALL: truncate(int fd, off_t size)
COM_SYS_SYSCALL(com_sys_syscall_truncate) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    int   fd   = COM_SYS_SYSCALL_ARG(int, 1);
    off_t size = COM_SYS_SYSCALL_ARG(off_t, 2);

    if (size < 0) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    com_proc_t *curr = ARCH_CPU_GET_THREAD()->proc;
    com_file_t *file = com_sys_proc_get_file(curr, fd);
    if (NULL == file) {
        return COM_SYS_SYSCALL_ERR(EBADF);
    }

    // POSIX.1-2017, ftruncate(): EINVAL if the file is not open for writing
    int accmode = O_ACCMODE & file->flags;
    int err     = EINVAL;
    if (O_WRONLY == accmode || O_RDWR == accmode) {
        err = com_fs_vfs_truncate(file->vnode, size);
    }

    COM_FS_FILE_RELEASE(file);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }
    return COM_SYS_SYSCALL_OK(0);
}
