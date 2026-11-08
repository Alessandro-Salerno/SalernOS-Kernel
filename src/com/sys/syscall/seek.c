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

// SYSCALL: seek(int fd, off_t offset, int whence)
COM_SYS_SYSCALL(com_sys_syscall_seek) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int   fd     = COM_SYS_SYSCALL_ARG(int, 1);
    off_t offset = COM_SYS_SYSCALL_ARG(off_t, 2);
    int   whence = COM_SYS_SYSCALL_ARG(int, 3);

    if (SEEK_SET != whence && SEEK_CUR != whence && SEEK_END != whence) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    com_syscall_ret_t ret  = COM_SYS_SYSCALL_BASE_OK();
    com_proc_t       *curr = ARCH_CPU_GET_THREAD()->proc;
    com_file_t       *file = com_sys_proc_get_file(curr, fd);

    if (NULL == file) {
        return COM_SYS_SYSCALL_ERR(EBADF);
    }

    int seekable = com_fs_vfs_seekable(file->vnode);
    if (0 != seekable) {
        COM_FS_FILE_RELEASE(file);
        return COM_SYS_SYSCALL_ERR(seekable);
    }

    off_t base = 0;
    if (SEEK_END == whence) {
        struct stat statbuf;
        int         vfs_ret = com_fs_vfs_stat(&statbuf, file->vnode, true);
        if (0 != vfs_ret) {
            ret = COM_SYS_SYSCALL_ERR(vfs_ret);
            goto cleanup;
        }
        base = statbuf.st_size;
    }

    kspinlock_acquire(&file->off_lock);
    if (SEEK_CUR == whence) {
        base = file->off;
    }

    off_t new_off;
    int   err = 0;
    if (__builtin_add_overflow(base, offset, &new_off)) {
        err = EOVERFLOW;
    } else if (new_off < 0) {
        err = EINVAL;
    } else {
        file->off = new_off;
    }
    kspinlock_release(&file->off_lock);

    ret = 0 != err ? COM_SYS_SYSCALL_ERR(err) : COM_SYS_SYSCALL_OK(new_off);

cleanup:
    KASSERT(NULL != file);
    COM_FS_FILE_RELEASE(file);
    return ret;
}
