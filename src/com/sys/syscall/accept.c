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
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <stdint.h>
#include <sys/socket.h>

// SYSCALL: accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen_ptr)
COM_SYS_SYSCALL(com_sys_syscall_accept) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int              sockfd   = COM_SYS_SYSCALL_ARG(int, 1);
    struct sockaddr *addr     = COM_SYS_SYSCALL_ARG(void *, 2);
    socklen_t       *addr_len = COM_SYS_SYSCALL_ARG(void *, 3);

    // TODO: handle addr output
    (void)addr;
    (void)addr_len;

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_syscall_ret_t ret = COM_SYS_SYSCALL_BASE_OK();

    com_file_t *sockfile = com_sys_proc_get_file(curr_proc, sockfd);
    if (NULL == sockfile) {
        ret = COM_SYS_SYSCALL_ERR(EBADF);
        goto end;
    }

    if (NULL == sockfile->vnode ||
        E_COM_VNODE_TYPE_SOCKET != sockfile->vnode->type) {
        ret = COM_SYS_SYSCALL_ERR(ENOTSOCK);
        goto end;
    }

    com_socket_vnode_t *server_vn = (void *)sockfile->vnode;
    KASSERT(NULL != server_vn->socket);
    com_socket_t *server_socket = server_vn->socket;

    com_socket_addr_t new_client_addr;
    com_socket_t     *new_client;
    int               sock_ret = COM_IPC_SOCKET_ACCEPT(&new_client,
                                         &new_client_addr,
                                         server_socket,
                                         sockfile->flags);
    if (0 != sock_ret) {
        ret = COM_SYS_SYSCALL_ERR(sock_ret);
        goto end;
    }

    int new_fd = com_sys_proc_next_fd(curr_proc);
    if (-1 == new_fd) {
        ret = COM_SYS_SYSCALL_ERR(EMFILE);
        goto end;
    }

    com_vnode_t        *client_vn = com_fs_sockfs_new();
    com_socket_vnode_t *sockfs_vn = (void *)client_vn;
    sockfs_vn->socket             = new_client;

    curr_proc->fd[new_fd].file = com_mm_slab_alloc(sizeof(com_file_t));
    com_file_t *new_file       = curr_proc->fd[new_fd].file;
    new_file->vnode            = client_vn;
    new_file->flags            = 0;
    new_file->num_ref          = 1;
    new_file->off              = 0;

    ret = COM_SYS_SYSCALL_OK(new_fd);

end:
    COM_FS_FILE_RELEASE(sockfile);
    return ret;
}
