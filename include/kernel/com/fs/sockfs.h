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

#pragma once

#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/socket/socket.h>

typedef struct com_socket_vnode {
    com_vnode_t   vnode;
    com_socket_t *socket;
} com_socket_vnode_t;

int com_fs_sockfs_readv(kioviter_t  *ioviter,
                        size_t      *bytes_read,
                        com_vnode_t *node,
                        uintmax_t    off,
                        uintmax_t    flags);
int com_fs_sockfs_writev(size_t      *bytes_written,
                         com_vnode_t *node,
                         kioviter_t  *ioviter,
                         uintmax_t    off,
                         uintmax_t    flags);

int com_fs_sockfs_ioctl(com_vnode_t *node, uintmax_t op, void *buf);
int com_fs_sockfs_poll_head(struct com_poll_head **out, com_vnode_t *node);
int com_fs_sockfs_poll(short *revents, com_vnode_t *node, short events);
int com_fs_sockfs_close(com_vnode_t *node);

// OTHER FUNCTIONS

com_vnode_t *com_fs_sockfs_new(void);
