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
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <stdint.h>

// SYSCALL: socket(int domain, int type, int protocol)
COM_SYS_SYSCALL(com_sys_syscall_socket) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int domain   = COM_SYS_SYSCALL_ARG(int, 1);
    int type     = COM_SYS_SYSCALL_ARG(int, 2);
    int protocol = COM_SYS_SYSCALL_ARG(int, 3);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_socket_type_t sys_type = com_ipc_socket_type_from_abi(domain,
                                                              type,
                                                              protocol);
    if (E_COM_SOCKET_TYPE_INVAL == sys_type) {
        return COM_SYS_SYSCALL_ERR(EAFNOSUPPORT);
    }

    int fd = com_sys_proc_next_fd(curr_proc);
    if (-1 == fd) {
        return COM_SYS_SYSCALL_ERR(EMFILE);
    }

    com_socket_t       *socket    = com_ipc_socket_new(sys_type);
    com_vnode_t        *socket_vn = com_fs_sockfs_new();
    com_socket_vnode_t *sockfs_vn = (void *)socket_vn;
    sockfs_vn->socket             = socket;

    curr_proc->fd[fd].file = com_mm_slab_alloc(sizeof(com_file_t));
    com_file_t *file       = curr_proc->fd[fd].file;
    file->vnode            = socket_vn;
    file->flags            = 0;
    file->num_ref          = 1;
    file->off              = 0;

    // Convert OSKC_* flags into O_* flags
    if (SOCK_NONBLOCK & type) {
        file->flags |= O_NONBLOCK;
    }
    if (SOCK_CLOEXEC & type) {
        file->flags |= O_CLOEXEC;
    }

    return COM_SYS_SYSCALL_OK(fd);
}
