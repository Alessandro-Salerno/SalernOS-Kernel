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
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/ioviter.h>
#include <lib/spinlock.h>
#include <sys/uio.h>

// SYSCALL: writev(int fd, struct iovec *iov, int iovcnt)
COM_SYS_SYSCALL(com_sys_syscall_writev) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int           fd     = COM_SYS_SYSCALL_ARG(int, 1);
    struct iovec *iov    = COM_SYS_SYSCALL_ARG(void *, 2);
    int           iovcnt = COM_SYS_SYSCALL_ARG(int, 3);

    com_syscall_ret_t ret = COM_SYS_SYSCALL_BASE_OK();

    com_proc_t *curr = ARCH_CPU_GET_THREAD()->proc;
    com_file_t *file = com_sys_proc_get_file(curr, fd);

    if (NULL == file) {
        ret = COM_SYS_SYSCALL_ERR(EBADF);
        return ret;
    }

    kioviter_t ioviter;
    kioviter_init(&ioviter, iov, iovcnt);
    size_t bytes_written = 0;
    int    vfs_op        = com_fs_vfs_writev(&bytes_written,
                                   file->vnode,
                                   &ioviter,
                                   file->off,
                                   file->flags);

    if (0 != vfs_op) {
        ret = COM_SYS_SYSCALL_ERR(vfs_op);
        goto cleanup;
    }

    kspinlock_acquire(&file->off_lock);
    file->off += bytes_written;
    kspinlock_release(&file->off_lock);

    ret = COM_SYS_SYSCALL_OK(bytes_written);
cleanup:
    COM_FS_FILE_RELEASE(file);
    return ret;
}
