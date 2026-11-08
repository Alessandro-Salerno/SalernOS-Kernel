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
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <stdint.h>

// SYSCALL: umount2(const struct syscall_str *target, int flags)
COM_SYS_SYSCALL(com_sys_syscall_umount2) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    const struct syscall_str *utarget = COM_SYS_SYSCALL_ARG(void *, 1);
    int                       flags   = COM_SYS_SYSCALL_ARG(int, 2);

    // Neither forced nor lazy unmounts exist
    if (0 != flags) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    char   path[CONFIG_PATH_MAX];
    size_t pathlen;
    int err = com_sys_syscall_copy_str(&pathlen, path, sizeof(path), utarget);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }

    com_proc_t  *curr_proc = ARCH_CPU_GET_THREAD()->proc;
    com_vnode_t *cwd       = com_sys_proc_get_cwd(curr_proc);
    com_vnode_t *target    = NULL;
    err                    = com_fs_vfs_lookup(&target,
                                               path,
                                               pathlen,
                                               curr_proc->root,
                                               cwd,
                                               COM_FS_VFS_LOOKUP_FOLLOW);
    COM_FS_VFS_VNODE_RELEASE(cwd);
    if (0 == err) {
        // The reference to target is consumed
        err = com_fs_vfs_unmount(target);
    }

    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }
    return COM_SYS_SYSCALL_OK(0);
}
