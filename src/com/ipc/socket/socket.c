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

#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/ipc/socket/unix.h>
#include <lib/str.h>
#include <sys/socket.h>
#include <sys/un.h>

com_socket_t *com_ipc_socket_new(com_socket_type_t type) {
    switch (type) {
        case E_COM_SOCKET_TYPE_UNIX: {
            com_socket_t *sun = com_ipc_socket_unix_new();
            sun->type         = type;
            return sun;
        }
        default:
            break;
    }

    return NULL;
}

int com_ipc_socket_addr_from_abi(com_socket_addr_t *out,
                                 struct sockaddr   *abi_addr,
                                 socklen_t          abi_addr_len) {
    (void)abi_addr_len;

    switch (abi_addr->sa_family) {
        case AF_UNIX: {
            struct sockaddr_un *unaddr = (void *)abi_addr;
            out->local.pathlen         = kstrlen(unaddr->sun_path);
            kmemcpy(out->local.path, unaddr->sun_path, out->local.pathlen);
            return 0;
        }

        default:
            break;
    }

    return EAFNOSUPPORT;
}

int com_ipc_socket_abi_from_addr(socklen_t         *abi_addr_len,
                                 struct sockaddr   *abi_addr,
                                 com_socket_addr_t *addr,
                                 com_socket_type_t  socktype,
                                 socklen_t          max_abi_addr_len) {
    switch (socktype) {
        case E_COM_SOCKET_TYPE_UNIX: {
            struct sockaddr_un *unaddr = (void *)abi_addr;
            unaddr->sun_family         = AF_UNIX;
            size_t copy_size = KMIN(addr->local.pathlen, max_abi_addr_len - 1);
            kmemcpy(unaddr->sun_path, addr->local.path, copy_size);
            unaddr->sun_path[copy_size] = 0;
            *abi_addr_len               = copy_size;
            return 0;
        }

        default:
            KASSERT(false);
            break;
    }

    return EAFNOSUPPORT;
}

com_socket_type_t
com_ipc_socket_type_from_abi(int domain, int type, int protocol) {
    (void)type;
    (void)protocol;

    switch (domain) {
        case AF_UNIX:
            return E_COM_SOCKET_TYPE_UNIX;
        default:
            break;
    }

    return E_COM_SOCKET_TYPE_INVAL;
}
