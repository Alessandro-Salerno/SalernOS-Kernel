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
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/spinlock.h>
#include <lib/str.h>
#include <stdatomic.h>

// SYSCALL: chdir(const char *path)
COM_SYS_SYSCALL(com_sys_syscall_chdir) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    const char *path = COM_SYS_SYSCALL_ARG(const char *, 1);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_vnode_t *old_cwd = atomic_load(&curr_proc->cwd);
    com_vnode_t *new_cwd = NULL;
    int          lk_ret  = com_fs_vfs_lookup(
        &new_cwd, path, kstrlen(path), curr_proc->root, old_cwd, true);

    if (0 != lk_ret || NULL == new_cwd) {
        return COM_SYS_SYSCALL_ERR(lk_ret);
    }

    if (E_COM_VNODE_TYPE_DIR != new_cwd->type) {
        COM_FS_VFS_VNODE_RELEASE(new_cwd);
        return COM_SYS_SYSCALL_ERR(ENOTDIR);
    }

    atomic_store(&curr_proc->cwd, new_cwd);
    COM_FS_VFS_VNODE_RELEASE(old_cwd);

    return COM_SYS_SYSCALL_OK(0);
}
