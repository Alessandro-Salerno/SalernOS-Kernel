/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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
  // TODO: add mmap, munmap and other fast things
} com_dev_ops_t;

// VNODE OPS

int com_fs_devfs_close(COM_FS_VFS_VNODE_t *vnode);
int com_fs_devfs_create(COM_FS_VFS_VNODE_t **out,
                        COM_FS_VFS_VNODE_t  *dir,
                        const char   *name,
                        size_t        namelen,
                        uintmax_t     attr);
int com_fs_devfs_mkdir(COM_FS_VFS_VNODE_t **out,
                       COM_FS_VFS_VNODE_t  *parent,
                       const char   *name,
                       size_t        namelen,
                       uintmax_t     attr);
int com_fs_devfs_read(void        *buf,
                      size_t       buflen,
                      size_t      *bytes_read,
                      COM_FS_VFS_VNODE_t *node,
                      uintmax_t    off,
                      uintmax_t    flags);
int com_fs_devfs_write(size_t      *bytes_written,
                       COM_FS_VFS_VNODE_t *node,
                       void        *buf,
                       size_t       buflen,
                       uintmax_t    off,
                       uintmax_t    flags);
int com_fs_devfs_ioctl(COM_FS_VFS_VNODE_t *node, uintmax_t op, void *buf);

// OTHER FUNCTIONS

int com_fs_devfs_register(COM_FS_VFS_VNODE_t  **out,
                          COM_FS_VFS_VNODE_t   *dir,
                          const char    *name,
                          size_t         namelen,
                          com_dev_ops_t *devops,
                          void          *devdata);
int com_fs_devfs_init(com_vfs_t **out, com_vfs_t *rootfs);
