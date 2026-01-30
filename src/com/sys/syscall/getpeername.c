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

// SYSCALL: getpeername(int sockfd, struct sockaddr *addr, socklen_t
// max_addrlen)
COM_SYS_SYSCALL(com_sys_syscall_getpeername) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int              sockfd      = COM_SYS_SYSCALL_ARG(int, 1);
    struct sockaddr *addr        = COM_SYS_SYSCALL_ARG(void *, 2);
    socklen_t        max_addrlen = COM_SYS_SYSCALL_ARG(socklen_t, 3);

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

    com_socket_vnode_t *socket_vn = (void *)sockfile->vnode;
    KASSERT(NULL != socket_vn->socket);
    com_socket_t *socket = socket_vn->socket;

    com_socket_addr_t peer_addr;
    int               sock_ret = COM_IPC_SOCKET_GETPEERNAME(&peer_addr, socket);
    if (0 != sock_ret) {
        ret = COM_SYS_SYSCALL_ERR(sock_ret);
    }

    socklen_t addr_len;
    sock_ret = com_ipc_socket_abi_from_addr(&addr_len,
                                            addr,
                                            &peer_addr,
                                            socket->type,
                                            max_addrlen);
    if (0 != sock_ret) {
        ret = COM_SYS_SYSCALL_ERR(sock_ret);
        goto end;
    }

    ret = COM_SYS_SYSCALL_OK(addr_len);

end:
    COM_FS_FILE_RELEASE(sockfile);
    return ret;
}
