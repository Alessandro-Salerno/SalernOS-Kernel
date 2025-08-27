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
#include <asm/ioctls.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/spinlock.h>
#include <lib/util.h>

#define FDIOCTL_OK(n)                              \
    (struct fdioctl_ret) {                         \
        .ret = COM_SYS_SYSCALL_OK(n), .done = true \
    }
#define FDIOCTL_ERR(errno)                              \
    (struct fdioctl_ret) {                              \
        .ret = COM_SYS_SYSCALL_ERR(errno), .done = true \
    }
#define FDIOCTL_NOP()      \
    (struct fdioctl_ret) { \
        .done = false      \
    }

struct fdioctl_ret {
    com_syscall_ret_t ret;
    bool              done;
};

// For ioctls that some unholy creature decided should be ioctls and not fcntls
static struct fdioctl_ret
fd_ioctl(int fd, uintmax_t op, void *buf, com_proc_t *proc) {
    (void)buf; // temporary probably
    kspinlock_acquire(&proc->fd_lock);
    com_filedesc_t *fildesc = com_sys_proc_get_fildesc_nolock(proc, fd);
    if (NULL == fildesc) {
        return FDIOCTL_ERR(EBADF);
    }

    struct fdioctl_ret ret = FDIOCTL_NOP();

    if (FIOCLEX == op) {
        fildesc->flags |= FD_CLOEXEC;
        ret = FDIOCTL_OK(0);
    } else if (FIONREAD == op) {
        ret = FDIOCTL_OK(0);
    }

    kspinlock_release(&proc->fd_lock);
    return ret;
}

// SYSCALL: ioctl(int fd, int op, void *buf)
COM_SYS_SYSCALL(com_sys_syscall_ioctl) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int       fd  = COM_SYS_SYSCALL_ARG(int, 1);
    uintmax_t op  = COM_SYS_SYSCALL_ARG(uintmax_t, 2);
    void     *buf = COM_SYS_SYSCALL_ARG(void *, 3);

    com_syscall_ret_t ret       = COM_SYS_SYSCALL_BASE_OK();
    com_proc_t       *curr_proc = ARCH_CPU_GET_THREAD()->proc;

    struct fdioctl_ret fdioctl_ret = fd_ioctl(fd, op, buf, curr_proc);
    if (fdioctl_ret.done) {
        return fdioctl_ret.ret;
    }

    com_file_t *file = com_sys_proc_get_file(curr_proc, fd);
    if (NULL == file) {
        ret = COM_SYS_SYSCALL_ERR(EBADF);
        goto end;
    }

    int vfs_op = com_fs_vfs_ioctl(file->vnode, op, buf);

    if (0 != vfs_op) {
        ret = COM_SYS_SYSCALL_ERR(vfs_op);
        goto end;
    }

end:
    COM_FS_FILE_RELEASE(file);
    return ret;
}
