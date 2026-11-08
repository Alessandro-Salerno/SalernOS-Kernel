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

#include <errno.h>
#include <kernel/com/fs/sockfs.h>

static com_vnode_ops_t SockfsNodeOps = {.readv     = com_fs_sockfs_readv,
                                        .writev    = com_fs_sockfs_writev,
                                        .ioctl     = com_fs_sockfs_ioctl,
                                        .poll_head = com_fs_sockfs_poll_head,
                                        .poll      = com_fs_sockfs_poll,
                                        .inactive  = com_fs_sockfs_inactive,
                                        .seekable  = com_fs_sockfs_seekable};

int com_fs_sockfs_readv(kioviter_t  *ioviter,
                        size_t      *bytes_read,
                        com_vnode_t *node,
                        uintmax_t    off,
                        uintmax_t    flags) {
    (void)off;
    com_socket_vnode_t *sockfs_vn = (void *)node;
    com_socket_desc_t   sock_desc = {.addr       = NULL,
                                     .ioviter    = ioviter,
                                     .count      = ioviter->total_size,
                                     .flags      = flags,
                                     .count_done = 0};

    int ret     = COM_IPC_SOCKET_RECV(sockfs_vn->socket, &sock_desc);
    *bytes_read = sock_desc.count_done;
    return ret;
}

int com_fs_sockfs_writev(size_t      *bytes_written,
                         com_vnode_t *node,
                         kioviter_t  *ioviter,
                         uintmax_t    off,
                         uintmax_t    flags) {
    (void)off;
    com_socket_vnode_t *sockfs_vn = (void *)node;
    com_socket_desc_t   sock_desc = {.addr       = NULL,
                                     .ioviter    = ioviter,
                                     .count      = ioviter->total_size,
                                     .flags      = flags,
                                     .count_done = 0};

    int ret        = COM_IPC_SOCKET_SEND(sockfs_vn->socket, &sock_desc);
    *bytes_written = sock_desc.count_done;
    return ret;
}

int com_fs_sockfs_ioctl(com_vnode_t *node, uintmax_t op, void *buf) {
    com_socket_vnode_t *sockfs_vn = (void *)node;
    com_socket_t       *socket    = sockfs_vn->socket;
    return COM_IPC_SOCKET_IOCTL(socket, op, buf);
}

int com_fs_sockfs_poll_head(struct com_poll_head **out, com_vnode_t *node) {
    com_socket_vnode_t *sockfs_vn = (void *)node;
    return COM_IPC_SOCKET_POLLHEAD(out, sockfs_vn->socket);
}

int com_fs_sockfs_poll(short *revents, com_vnode_t *node, short events) {
    com_socket_vnode_t *sockfs_vn = (void *)node;
    return COM_IPC_SOCKET_POLL(revents, sockfs_vn->socket, events);
}

// Socket vnodes are only referenced by their files and operations in progress
void com_fs_sockfs_inactive(com_vnode_t *node) {
    com_socket_vnode_t *sockfs_vn = (void *)node;
    KASSERT_CALL(0, ==, COM_IPC_SOCKET_DESTROY(sockfs_vn->socket));
    com_mm_slab_free(sockfs_vn, sizeof(com_socket_vnode_t));
}

int com_fs_sockfs_seekable(com_vnode_t *node) {
    (void)node;
    return ESPIPE;
}

// OTHER FUNCTIONS

com_vnode_t *com_fs_sockfs_new(void) {
    com_socket_vnode_t *sockfs_vn = com_mm_slab_alloc(
        sizeof(com_socket_vnode_t));
    com_fs_vfs_vnode_init(&sockfs_vn->vnode,
                          NULL,
                          E_COM_VNODE_TYPE_SOCKET,
                          &SockfsNodeOps,
                          NULL);
    return &sockfs_vn->vnode;
}
