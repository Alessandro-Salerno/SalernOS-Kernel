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

com_syscall_ret_t com_sys_syscall_open(arch_context_t *ctx,
                                       uintmax_t       pathptr,
                                       uintmax_t       pathlen,
                                       uintmax_t       flags,
                                       uintmax_t       unused) {
    (void)ctx;
    (void)unused;
    char             *path    = (char *)pathptr;
    com_syscall_ret_t ret     = {0};
    com_proc_t       *curr    = hdr_arch_cpu_get_thread()->proc;
    com_vnode_t      *file_vn = NULL;

    /*if (O_CREAT & flags) {
        com_vnode_t *exist = NULL;
        com_vnode_t *cwd   = atomic_load(&curr->cwd);
        int lk_ret = com_fs_vfs_lookup(&exist, path, pathlen, curr->root, cwd);

        if ((O_EXCL & flags) && 0 == lk_ret) {
            COM_FS_VFS_VNODE_RELEASE(exist);
            ret.err = EEXIST;
            goto cleanup;
        }

        // TODO: create file
    } else*/
    {
        com_vnode_t *cwd = atomic_load(&curr->cwd);
        KDEBUG("opening file %s with root=%x cwd=%x", path, curr->root, cwd);
        int lk_ret =
            com_fs_vfs_lookup(&file_vn, path, pathlen, curr->root, cwd);

        if (0 != lk_ret) {
            ret.err = lk_ret;
            goto cleanup;
        }
    }

    uintmax_t fd      = com_sys_proc_next_fd(curr);
    curr->fd[fd].file = com_mm_slab_alloc(sizeof(com_file_t));
    com_file_t *file  = curr->fd[fd].file;
    file->vnode       = file_vn;
    file->flags       = flags;
    file->num_ref     = 1;
    file->off         = 0;
    ret.value         = fd;

cleanup:
    return ret;
}
