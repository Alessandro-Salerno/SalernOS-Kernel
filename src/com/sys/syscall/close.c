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
#include <lib/util.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

// SYSCALL: close(int fd)
COM_SYS_SYSCALL(com_sys_syscall_close) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    int fd = COM_SYS_SYSCALL_ARG(int, 1);

    if (fd < 0 || fd > COM_SYS_PROC_MAX_FDS) {
        return COM_SYS_SYSCALL_ERR(EBADF);
    }

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_spinlock_acquire(&curr_proc->fd_lock);
    com_filedesc_t *fildesc = &curr_proc->fd[fd];

    if (NULL == fildesc->file) {
        com_spinlock_release(&curr_proc->fd_lock);
        return COM_SYS_SYSCALL_ERR(EBADF);
    }

    KDEBUG("closing fd=%d with refcount=%d (vn=%x)",
           fd,
           fildesc->file->num_ref,
           fildesc->file->vnode);
    COM_FS_FILE_RELEASE(fildesc->file);
    KDEBUG("after close, file=%x", fildesc->file);
    *fildesc = (com_filedesc_t){0};

    // Recycle file descriptors
    if (fd < curr_proc->next_fd) {
        curr_proc->next_fd = fd;
    }

    com_spinlock_release(&curr_proc->fd_lock);
    return COM_SYS_SYSCALL_OK(0);
}
