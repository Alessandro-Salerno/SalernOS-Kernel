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
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/spinlock.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

// SYSCALL: getcwd(char *buf, size_t size)
COM_SYS_SYSCALL(com_sys_syscall_getcwd) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    char  *buf  = COM_SYS_SYSCALL_ARG(char *, 1);
    size_t size = COM_SYS_SYSCALL_ARG(size_t, 2);

    if (size < 1) {
        return COM_SYS_SYSCALL_ERR(EOVERFLOW);
    }

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;
    com_vnode_t  *curr_cwd    = atomic_load(&curr_proc->cwd);

    int          err    = 0;
    com_vnode_t *parent = NULL;
    ssize_t      place_idx =
        (ssize_t)size - 2; // if you're above that, that's your problem
    buf[size - 1] = 0;     // string terminator

    while (0 == err) {
        com_vnctl_name_t name;
        err = com_fs_vfs_vnctl(curr_cwd, COM_FS_VFS_VNCTL_GETNAME, &name);

        if (0 != err) {
            // parent is either NULL (operation discarded) or equal to curr_cwd.
            // If parent == curr_cwd, however, it means that the curr_cwd is not
            // the original, so in case of failure it needs to be released
            COM_FS_VFS_VNODE_RELEASE(parent);
            break;
        }

        place_idx -= name.namelen - 1;
        if (place_idx - 1 < 0) { // -1 for the /
            // same as above
            COM_FS_VFS_VNODE_RELEASE(parent);
            err = EOVERFLOW;
            break;
        }

        kmemcpy(&buf[place_idx], name.name, name.namelen);
        if ('/' != buf[place_idx]) { // avoid //usr caes
            buf[place_idx - 1] = '/';
            place_idx -= 2; // currently, it points to the first letter of the
                            // current name, thus -1 is the /, -2 is the last
                            // slot for the previous name
        } else {
            place_idx--; // there's no path sep, so just -1
        }

        // If namelen == 0, then we reached the root
        if (0 == name.namelen) {
            break;
        }

        com_vnode_t *old_parent = parent;
        err                     = com_fs_vfs_lookup(
            &parent, "..", 2, curr_proc->root, curr_cwd, true);

        if (0 == err) {
            // here we release the old parent.
            // This was either NULL (discarded) or a previously acquired
            // directory The original cwd is never released
            COM_FS_VFS_VNODE_RELEASE(old_parent);
            curr_cwd = parent;
        }
    }

    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }

    size_t written = size - place_idx - 1;
    kmemmove(buf, &buf[place_idx + 1], written);

    return COM_SYS_SYSCALL_OK((uintptr_t)buf);
}
