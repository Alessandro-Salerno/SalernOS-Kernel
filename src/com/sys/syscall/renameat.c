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
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <stdbool.h>
#include <stdint.h>

// SYSCALL: renameat(int old_dir_fd, const struct syscall_str *oldpath,
// int new_dir_fd, const struct syscall_str *newpath)
COM_SYS_SYSCALL(com_sys_syscall_renameat) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(5);

    int                       old_dir_fd = COM_SYS_SYSCALL_ARG(int, 1);
    const struct syscall_str *uoldpath   = COM_SYS_SYSCALL_ARG(void *, 2);
    int                       new_dir_fd = COM_SYS_SYSCALL_ARG(int, 3);
    const struct syscall_str *unewpath   = COM_SYS_SYSCALL_ARG(void *, 4);

    char   oldpath[CONFIG_PATH_MAX];
    size_t oldpathlen;
    int    oldpath_ret = com_sys_syscall_copy_str(&oldpathlen,
                                                  oldpath,
                                                  sizeof(oldpath),
                                                  uoldpath);
    if (0 != oldpath_ret) {
        return COM_SYS_SYSCALL_ERR(oldpath_ret);
    }

    char   newpath[CONFIG_PATH_MAX];
    size_t newpathlen;
    int    newpath_ret = com_sys_syscall_copy_str(&newpathlen,
                                                  newpath,
                                                  sizeof(newpath),
                                                  unewpath);
    if (0 != newpath_ret) {
        return COM_SYS_SYSCALL_ERR(newpath_ret);
    }

    com_proc_t  *curr_proc  = ARCH_CPU_GET_THREAD()->proc;
    com_vnode_t *old_dir    = NULL;
    com_vnode_t *new_dir    = NULL;
    com_vnode_t *old_parent = NULL;
    com_vnode_t *new_parent = NULL;
    int err = com_sys_proc_get_directory(&old_dir, curr_proc, old_dir_fd);
    if (0 == err) {
        err = com_sys_proc_get_directory(&new_dir, curr_proc, new_dir_fd);
    }
    if (0 != err) {
        goto end;
    }

    const char *old_name, *new_name;
    size_t      old_namelen, new_namelen;
    bool        old_must_dir, new_must_dir;
    err = com_fs_vfs_lookup_parent(&old_parent,
                                   &old_name,
                                   &old_namelen,
                                   &old_must_dir,
                                   oldpath,
                                   oldpathlen,
                                   curr_proc->root,
                                   old_dir);
    if (0 != err) {
        goto end;
    }

    err = com_fs_vfs_lookup_parent(&new_parent,
                                   &new_name,
                                   &new_namelen,
                                   &new_must_dir,
                                   newpath,
                                   newpathlen,
                                   curr_proc->root,
                                   new_dir);
    if (0 != err) {
        goto end;
    }

    err = com_fs_vfs_rename(old_parent,
                            old_name,
                            old_namelen,
                            new_parent,
                            new_name,
                            new_namelen,
                            (old_must_dir || new_must_dir) ? COM_FS_VFS_MUST_DIR
                                                           : 0);

end:
    COM_FS_VFS_VNODE_RELEASE(new_parent);
    COM_FS_VFS_VNODE_RELEASE(old_parent);
    COM_FS_VFS_VNODE_RELEASE(new_dir);
    COM_FS_VFS_VNODE_RELEASE(old_dir);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }
    return COM_SYS_SYSCALL_OK(0);
}
