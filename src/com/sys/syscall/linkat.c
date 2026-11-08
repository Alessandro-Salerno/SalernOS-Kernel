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

// SYSCALL: linkat(int old_dir_fd, const struct syscall_str *oldpath,
// int new_dir_fd, const struct syscall_str *newpath, int flags)
COM_SYS_SYSCALL(com_sys_syscall_linkat) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(6);

    int                       old_dir_fd = COM_SYS_SYSCALL_ARG(int, 1);
    const struct syscall_str *uoldpath   = COM_SYS_SYSCALL_ARG(void *, 2);
    int                       new_dir_fd = COM_SYS_SYSCALL_ARG(int, 3);
    const struct syscall_str *unewpath   = COM_SYS_SYSCALL_ARG(void *, 4);
    int                       flags      = COM_SYS_SYSCALL_ARG(int, 5);

    if (~AT_SYMLINK_FOLLOW & flags) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

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

    com_proc_t  *curr_proc = ARCH_CPU_GET_THREAD()->proc;
    com_vnode_t *old_dir   = NULL;
    com_vnode_t *new_dir   = NULL;
    com_vnode_t *node      = NULL;
    com_vnode_t *parent    = NULL;
    int err = com_sys_proc_get_directory(&old_dir, curr_proc, old_dir_fd);
    if (0 == err) {
        err = com_sys_proc_get_directory(&new_dir, curr_proc, new_dir_fd);
    }
    if (0 != err) {
        goto end;
    }

    err = com_fs_vfs_lookup(
        &node,
        oldpath,
        oldpathlen,
        curr_proc->root,
        old_dir,
        (AT_SYMLINK_FOLLOW & flags) ? COM_FS_VFS_LOOKUP_FOLLOW : 0);
    if (0 != err) {
        goto end;
    }

    const char *name;
    size_t      namelen;
    bool        must_dir;
    err = com_fs_vfs_lookup_parent(&parent,
                                   &name,
                                   &namelen,
                                   &must_dir,
                                   newpath,
                                   newpathlen,
                                   curr_proc->root,
                                   new_dir);
    if (0 != err) {
        goto end;
    }

    // A path that ends with '/' only names directories, which cannot be
    // linked, so it can only fail. POSIX.1-2017, 4.13 and link()
    if (must_dir && E_COM_VNODE_TYPE_DIR != node->type) {
        com_vnode_t *existing = NULL;
        err = com_fs_vfs_lookup(&existing, name, namelen, parent, parent, 0);
        COM_FS_VFS_VNODE_RELEASE(existing);
        err = 0 == err ? EEXIST : ENOENT;
        goto end;
    }

    err = com_fs_vfs_link(parent, name, namelen, node);

end:
    COM_FS_VFS_VNODE_RELEASE(parent);
    COM_FS_VFS_VNODE_RELEASE(node);
    COM_FS_VFS_VNODE_RELEASE(new_dir);
    COM_FS_VFS_VNODE_RELEASE(old_dir);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }
    return COM_SYS_SYSCALL_OK(0);
}
