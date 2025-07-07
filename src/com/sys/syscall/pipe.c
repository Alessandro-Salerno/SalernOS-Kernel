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
#include <kernel/com/fs/pipefs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <stdint.h>

// SYSCALL: pipe(int *fildes);
COM_SYS_SYSCALL(com_sys_syscall_pipe) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    int *fildes = COM_SYS_SYSCALL_ARG(int *, 1);

    com_proc_t *curr = hdr_arch_cpu_get_thread()->proc;
    // TODO: check if fds are available
    uintmax_t rfd = com_sys_proc_next_fd(curr);
    uintmax_t wfd = com_sys_proc_next_fd(curr);

    com_vnode_t *read;
    com_vnode_t *write;
    com_fs_pipefs_new(&read, &write);

    com_file_t *rf = com_mm_slab_alloc(sizeof(com_file_t));
    rf->vnode      = read;
    rf->flags      = 0;
    rf->num_ref    = 1;
    rf->off        = 0;

    com_file_t *wf = com_mm_slab_alloc(sizeof(com_file_t));
    wf->vnode      = write;
    wf->flags      = 0;
    wf->num_ref    = 1;
    wf->off        = 0;

    curr->fd[rfd].file = rf;
    curr->fd[wfd].file = wf;

    fildes[0] = rfd;
    fildes[1] = wfd;

    return COM_SYS_SYSCALL_OK(0);
}
