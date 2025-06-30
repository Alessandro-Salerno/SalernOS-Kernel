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

#include <kernel/com/fs/vfs.h>

typedef struct com_dev_ops {
    int (*read)(void     *buf,
                size_t    buflen,
                size_t   *bytes_read,
                void     *devdata,
                uintmax_t off,
                uintmax_t flags);
    int (*write)(size_t   *bytes_written,
                 void     *devdata,
                 void     *buf,
                 size_t    buflen,
                 uintmax_t off,
                 uintmax_t flags);
    int (*ioctl)(void *devdata, uintmax_t op, void *buf);
    int (*isatty)(void *devdata);
    // TODO: add mmap, munmap and other fast things
} com_dev_ops_t;

// VNODE OPS

int com_fs_devfs_close(com_vnode_t *vnode);
int com_fs_devfs_create(com_vnode_t **out,
                        com_vnode_t  *dir,
                        const char   *name,
                        size_t        namelen,
                        uintmax_t     attr);
int com_fs_devfs_mkdir(com_vnode_t **out,
                       com_vnode_t  *parent,
                       const char   *name,
                       size_t        namelen,
                       uintmax_t     attr);
int com_fs_devfs_read(void        *buf,
                      size_t       buflen,
                      size_t      *bytes_read,
                      com_vnode_t *node,
                      uintmax_t    off,
                      uintmax_t    flags);
int com_fs_devfs_write(size_t      *bytes_written,
                       com_vnode_t *node,
                       void        *buf,
                       size_t       buflen,
                       uintmax_t    off,
                       uintmax_t    flags);
int com_fs_devfs_ioctl(com_vnode_t *node, uintmax_t op, void *buf);
int com_fs_devfs_isatty(com_vnode_t *node);

// OTHER FUNCTIONS

int com_fs_devfs_register(com_vnode_t  **out,
                          com_vnode_t   *dir,
                          const char    *name,
                          size_t         namelen,
                          com_dev_ops_t *devops,
                          void          *devdata);
int com_fs_devfs_init(com_vfs_t **out, com_vfs_t *rootfs);
