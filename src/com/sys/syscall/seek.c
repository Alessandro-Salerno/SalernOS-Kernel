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

com_syscall_ret_t com_sys_syscall_seek(arch_context_t *ctx,
                                       uintmax_t       fd,
                                       uintmax_t       uoffset,
                                       uintmax_t       uwhence,
                                       uintmax_t       unused) {
    (void)ctx;
    (void)unused;

    off_t offset = (off_t)uoffset;
    int   whence = (int)uwhence;

    com_syscall_ret_t ret  = {0};
    com_proc_t       *curr = hdr_arch_cpu_get_thread()->proc;
    com_file_t       *file = com_sys_proc_get_file(curr, fd);

    if (NULL == file) {
        ret.err = EBADF;
        goto cleanup;
    }

    // TODO: handle errors (no seek on pipes etc, no seek out of bounds)

    hdr_com_spinlock_acquire(&file->off_lock);
    off_t new_off = file->off;
    switch (whence) {
    case SEEK_SET:
        new_off = offset;
        break;
    case SEEK_CUR:
        new_off += offset;
        break;
    case SEEK_END: {
        struct stat statbuf;
        int         vfs_ret = com_fs_vfs_stat(&statbuf, file->vnode);
        if (0 != vfs_ret) {
            ret.err = vfs_ret;
            goto cleanup;
        }
        new_off = statbuf.st_size + offset;
        break;
    }
    }

    file->off = new_off;
    ret.value = new_off;
cleanup:
    hdr_com_spinlock_release(&file->off_lock);
    COM_FS_FILE_RELEASE(file);
    return ret;
}
