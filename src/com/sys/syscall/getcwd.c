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
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/spinlock.h>
#include <lib/usercopy.h>
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
        return COM_SYS_SYSCALL_ERR(ERANGE);
    }

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;
    com_vnode_t  *cwd         = com_sys_proc_get_cwd(curr_proc);

    // A path that does not fit the user's buffer is ERANGE, one that does not
    // fit ours is ENAMETOOLONG
    char   kbuf[CONFIG_PATH_MAX];
    size_t ksize = KMIN(size, sizeof(kbuf));
    size_t start = 0;
    int    err = com_fs_vfs_get_path(&start, kbuf, ksize, cwd, curr_proc->root);
    COM_FS_VFS_VNODE_RELEASE(cwd);
    if (ERANGE == err && ksize != size) {
        err = ENAMETOOLONG;
    }
    if (0 == err) {
        err = kmemcpy_to_user(buf, &kbuf[start], ksize - start);
    }
    if (0 != err) {
        return COM_SYS_SYSCALL_ERR(err);
    }

    return COM_SYS_SYSCALL_OK((uintptr_t)buf);
}
