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
#include <sys/mount.h>

#define MOUNT_TYPE_MAX 32

// SYSCALL: mount(const struct syscall_str *source,
// const struct syscall_str *target, const struct syscall_str *fstype,
// unsigned long flags, const void *data)
COM_SYS_SYSCALL(com_sys_syscall_mount) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(6);

    const struct syscall_str *usource = COM_SYS_SYSCALL_ARG(void *, 1);
    const struct syscall_str *utarget = COM_SYS_SYSCALL_ARG(void *, 2);
    const struct syscall_str *ufstype = COM_SYS_SYSCALL_ARG(void *, 3);
    unsigned long             flags   = COM_SYS_SYSCALL_ARG(unsigned long, 4);
    const void               *data    = COM_SYS_SYSCALL_ARG(void *, 5);

    // No file system takes options yet
    if (NULL != data || 0 != (flags & ~(unsigned long)MS_RDONLY)) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    char   fstype[MOUNT_TYPE_MAX];
    size_t fstypelen;
    int    err = com_sys_syscall_copy_str(&fstypelen,
                                          fstype,
                                          sizeof(fstype),
                                          ufstype);
    if (ENAMETOOLONG == err) {
        return COM_SYS_SYSCALL_ERR(ENODEV);
    }
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }

    com_fs_type_t *type = com_fs_vfs_find_type(fstype, fstypelen);
    if (NULL == type) {
        return COM_SYS_SYSCALL_ERR(ENODEV);
    }

    // The path buffer is reused for the target once the source is found
    char         path[CONFIG_PATH_MAX];
    size_t       pathlen;
    com_proc_t  *curr_proc = ARCH_CPU_GET_THREAD()->proc;
    com_vnode_t *cwd       = com_sys_proc_get_cwd(curr_proc);
    com_vnode_t *source    = NULL;
    com_vnode_t *target    = NULL;
    if (type->needs_backing) {
        err = com_sys_syscall_copy_str(&pathlen, path, sizeof(path), usource);
        if (0 != err) {
            goto end;
        }

        err = com_fs_vfs_lookup(&source,
                                path,
                                pathlen,
                                curr_proc->root,
                                cwd,
                                COM_FS_VFS_LOOKUP_FOLLOW);
        if (0 != err) {
            goto end;
        }
    }

    err = com_sys_syscall_copy_str(&pathlen, path, sizeof(path), utarget);
    if (0 != err) {
        goto end;
    }

    err = com_fs_vfs_lookup(&target,
                            path,
                            pathlen,
                            curr_proc->root,
                            cwd,
                            COM_FS_VFS_LOOKUP_FOLLOW);
    if (0 != err) {
        goto end;
    }

    com_vfs_t *vfs = NULL;
    err = com_fs_vfs_mount(&vfs,
                           type,
                           source,
                           target,
                           (MS_RDONLY & flags) ? COM_FS_VFS_MOUNT_RDONLY : 0);

end:
    COM_FS_VFS_VNODE_RELEASE(target);
    COM_FS_VFS_VNODE_RELEASE(source);
    COM_FS_VFS_VNODE_RELEASE(cwd);
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }
    return COM_SYS_SYSCALL_OK(0);
}
