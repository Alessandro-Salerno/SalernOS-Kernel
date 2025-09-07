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
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/spinlock.h>
#include <lib/util.h>
#include <stdatomic.h>
#include <stdint.h>

// SYSCALL: dup3(int old_fd, int new_fd, int fd_flags)
COM_SYS_SYSCALL(com_sys_syscall_dup3) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    int old_fd = COM_SYS_SYSCALL_ARG(int, 1);
    int new_fd = COM_SYS_SYSCALL_ARG(int, 2);
    // int flags  = COM_SYS_SYSCALL_ARG(int, 3);
    // TODO: handle flags

    com_syscall_ret_t ret         = COM_SYS_SYSCALL_BASE_OK();
    com_thread_t     *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t       *curr_proc   = curr_thread->proc;

    kspinlock_acquire(&curr_proc->fd_lock);
    com_sys_proc_close_file_nolock(curr_proc, new_fd);
    int dup_ret = com_sys_proc_duplicate_file_nolock(curr_proc, new_fd, old_fd);
    if (dup_ret < 0) {
        ret = COM_SYS_SYSCALL_ERR(-dup_ret);
    }

    kspinlock_release(&curr_proc->fd_lock);
    return ret;
}
