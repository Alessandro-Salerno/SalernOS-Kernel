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

#pragma once

#include <errno.h>
#include <kernel/com/fs/vfs.h>
#include <lib/ioviter.h>
#include <lib/mutex.h>
#include <stddef.h>
#include <stdint.h>

#define __SOCKET_OP(sock, op, ...) \
    ((NULL != (sock)->ops->op) ? (sock)->ops->op(__VA_ARGS__) : ENOSYS)

#define COM_IPC_SOCKET_BIND(sock, addr) __SOCKET_OP(sock, bind, sock, addr)
#define COM_IPC_SOCKET_SEND(sock, desc) __SOCKET_OP(sock, send, sock, desc)
#define COM_IPC_SOCKET_RECV(sock, desc) __SOCKET_OP(sock, recv, sock, desc)
#define COM_IPC_SOCKET_CONNECT(sock, addr) \
    __SOCKET_OP(sock, connect, sock, addr)
#define COM_IPC_SOCKET_LISTEN(sock) __SOCKET_OP(sock, listen, sock)
#define COM_IPC_SOCKET_ACCEPT(server, client, addr, flags) \
    __SOCKET_OP(server, accept, server, client, addr, flags)
#define COM_IPC_SOCKET_GETNAME(out, sock) __SOCKET_OP(sock, getname, out, sock)
#define COM_IPC_SOCKET_GETPEERNAME(out, sock) \
    __SOCKET_OP(sock, getpeername, out, sock)
#define COM_IPC_SOCKET_POLLHEAD(out, sock) \
    __SOCKET_OP(sock, poll_head, out, sock)
#define COM_IPC_SOCKET_POLL(revents, sock, events) \
    __SOCKET_OP(sock, poll, revents, sock, events)
#define COM_IPC_SOCKET_DESTROY(sock) __SOCKET_OP(sock, destroy, sock)

typedef enum com_socket_type {
    E_COM_SOCKET_TYPE_UNIX
} com_socket_type_t;

typedef enum com_socket_state {
    E_COM_SOCKET_STATE_UNBOUND,
    E_COM_SOCKET_STATE_BOUND,
    E_COM_SOCKET_STATE_CONNECTED,
    E_COM_SOCKET_STATE_LISTENING
} com_socket_state_t;

typedef union com_socket_addr {
    const char *path; // For TYPE_LOCAL
} com_socket_addr_t;

typedef struct com_socket {
    com_socket_type_t      type;
    struct com_socket_ops *ops;
    kmutex_t               mutex;
    com_socket_state_t     state;
} com_socket_t;

typedef struct com_socket_desc {
    com_socket_addr_t *addr;
    kioviter_t        *ioviter;
    size_t             count;
    uintmax_t          flags;
    size_t             count_done;
} com_socket_desc_t;

typedef struct com_socket_ops {
    int (*bind)(com_socket_t *socket, com_socket_addr_t *addr);
    int (*send)(com_socket_t *socket, com_socket_desc_t *desc);
    int (*recv)(com_socket_t *socket, com_socket_desc_t *desc);
    int (*connect)(com_socket_t *socket, com_socket_addr_t *addr);
    int (*listen)(com_socket_t *socket);
    int (*accept)(com_socket_t      *server,
                  com_socket_t      *client,
                  com_socket_addr_t *addr,
                  uintmax_t          flags);
    int (*getname)(com_socket_addr_t *out, com_socket_t *socket);
    int (*getpeername)(com_socket_addr_t *out, com_socket_t *socket);
    int (*poll_head)(struct com_poll_head **out, com_socket_t *socket);
    int (*poll)(short *revents, com_socket_t *socket, short events);
    int (*destroy)(com_socket_t *socket);
} com_socket_ops_t;
