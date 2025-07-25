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

static com_syscall_ret_t
fcntl_dup(com_proc_t *curr_proc, int fd, int op, int arg1) {
    int dup_ret = com_sys_proc_duplicate_file(curr_proc, arg1, fd);

    if (dup_ret < 0) {
        KASSERT(false);
        return COM_SYS_SYSCALL_ERR(-dup_ret);
    }

    if (F_DUPFD_CLOEXEC == op) {
        com_filedesc_t *fildesc = com_sys_proc_get_fildesc(curr_proc, dup_ret);
        fildesc->flags |= FD_CLOEXEC;
        COM_FS_FILE_RELEASE(fildesc->file);
    }

    return COM_SYS_SYSCALL_OK(dup_ret);
}

// SYSCALL: fcntl(fd, op, arg1)
COM_SYS_SYSCALL(com_sys_syscall_fcntl) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int fd   = COM_SYS_SYSCALL_ARG(int, 1);
    int op   = COM_SYS_SYSCALL_ARG(int, 2);
    int arg1 = COM_SYS_SYSCALL_ARG(int, 3);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_syscall_ret_t ret    = COM_SYS_SYSCALL_BASE_OK();
    com_filedesc_t   *fildes = com_sys_proc_get_fildesc(curr_proc, fd);
    com_file_t       *file   = com_sys_proc_get_file(curr_proc, fd);

    if (NULL == file || NULL == fildes) {
        KDEBUG("%d is not a valid fd, arg1=%d", fd, arg1);
        return COM_SYS_SYSCALL_ERR(EBADF);
    }

    if (F_GETFL == op) {
        ret.value = file->flags;
        goto end;
    }

    if (F_GETFD == op) {
        ret.value = fildes->flags;
        goto end;
    }

    if (F_SETFL == op) {
        // TODO: probably wrong
        fildes->flags = arg1;
        goto end;
    }

    if (F_DUPFD == op || F_DUPFD_CLOEXEC == op) {
        ret = fcntl_dup(curr_proc, fd, op, arg1);
        goto end;
    }

    ret = COM_SYS_SYSCALL_ERR(ENOSYS);

end:
    // NOTE: these are the same, but they're also held twice above
    COM_FS_FILE_RELEASE(file);
    COM_FS_FILE_RELEASE(fildes->file);
    return ret;
}
