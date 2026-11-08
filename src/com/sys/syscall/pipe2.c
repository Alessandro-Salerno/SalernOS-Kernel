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
#include <kernel/com/fs/pipefs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/usercopy.h>
#include <lib/util.h>
#include <stdint.h>

// SYSCALL: pipe2(int *fildes, int flags);
COM_SYS_SYSCALL(com_sys_syscall_pipe2) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    int *fildes = COM_SYS_SYSCALL_ARG(int *, 1);
    int  flags  = COM_SYS_SYSCALL_ARG(int, 2);

    com_proc_t *curr = ARCH_CPU_GET_THREAD()->proc;
    int         rfd  = com_sys_proc_next_fd(curr);
    int         wfd  = -1 != rfd ? com_sys_proc_next_fd(curr) : -1;

    if (-1 == wfd) {
        if (-1 != rfd) {
            com_sys_proc_unreserve_fd(curr, rfd);
        }
        return COM_SYS_SYSCALL_ERR(EMFILE);
    }

    com_vnode_t *read;
    com_vnode_t *write;
    com_fs_pipefs_new(&read, &write);

    com_file_t *rf = com_mm_slab_alloc(sizeof(com_file_t));
    rf->vnode      = read;
    rf->flags      = O_NONBLOCK & flags;
    rf->num_ref    = 1;
    rf->off        = 0;

    com_file_t *wf = com_mm_slab_alloc(sizeof(com_file_t));
    wf->vnode      = write;
    wf->flags      = O_NONBLOCK & flags;
    wf->num_ref    = 1;
    wf->off        = 0;

    uintmax_t fd_flags = (O_CLOEXEC & flags) ? FD_CLOEXEC : 0;
    com_sys_proc_install_fd(curr, rfd, rf, fd_flags);
    com_sys_proc_install_fd(curr, wfd, wf, fd_flags);

    int kfildes[2] = {rfd, wfd};
    int copy_ret   = kmemcpy_to_user(fildes, kfildes, sizeof(kfildes));
    if (0 != copy_ret) {
        com_sys_proc_close_file(curr, rfd);
        com_sys_proc_close_file(curr, wfd);
        return COM_SYS_SYSCALL_ERR(copy_ret);
    }
    KDEBUG("returning fd=(%d, %d), read_vn=%p, write_vn=%p",
           rfd,
           wfd,
           read,
           write);

    return COM_SYS_SYSCALL_OK(0);
}
