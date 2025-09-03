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
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <stdint.h>
#include <sys/socket.h>

// SYSCALL: bind(int sockfd, struct sockaddr *addr, socklen_t addr_len)
COM_SYS_SYSCALL(com_sys_syscall_bind) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int              sockfd   = COM_SYS_SYSCALL_ARG(int, 1);
    struct sockaddr *addr     = COM_SYS_SYSCALL_ARG(void *, 2);
    socklen_t        addr_len = COM_SYS_SYSCALL_ARG(socklen_t, 3);

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

    com_socket_addr_t sys_addr;
    int sock_ret = com_ipc_socket_addr_from_abi(&sys_addr, addr, addr_len);
    if (0 != sock_ret) {
        ret = COM_SYS_SYSCALL_ERR(sock_ret);
        goto end;
    }

    sock_ret = COM_IPC_SOCKET_BIND(socket, &sys_addr);
    if (0 != sock_ret) {
        ret = COM_SYS_SYSCALL_ERR(sock_ret);
    }

end:
    COM_FS_FILE_RELEASE(sockfile);
    return ret;
}
