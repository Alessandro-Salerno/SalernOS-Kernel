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
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>

com_syscall_ret_t com_sys_syscall_read(arch_context_t *ctx,
                                       uintmax_t       fd,
                                       uintmax_t       bufptr,
                                       uintmax_t       buflen,
                                       uintmax_t       unused) {
    (void)ctx;
    (void)unused;
    void             *buf = (void *)bufptr;
    com_syscall_ret_t ret = {0};

    com_proc_t *curr = hdr_arch_cpu_get_thread()->proc;
    com_file_t *file = com_sys_proc_get_file(curr, fd);

    if (NULL == file) {
        ret.err = EBADF;
        return ret;
    }

    size_t bytes_read = 0;
    int    vfs_op =
        com_fs_vfs_read(buf, buflen, &bytes_read, file->vnode, file->off, 0);

    if (0 != vfs_op) {
        ret.err = vfs_op;
        goto cleanup;
    }

    hdr_com_spinlock_acquire(&file->off_lock);
    file->off += bytes_read;
    hdr_com_spinlock_release(&file->off_lock);

    ret.value = bytes_read;
cleanup:
    return ret;
}
