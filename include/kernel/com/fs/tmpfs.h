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

// VFS OPS

int com_fs_tmpfs_vget(com_vnode_t **out, com_vfs_t *vfs, void *inode);
int com_fs_tmpfs_mount(com_vfs_t **out, com_vnode_t *mountpoint);

// VNODE OPS

int com_fs_tmpfs_create(com_vnode_t **out,
                        com_vnode_t  *dir,
                        const char   *name,
                        size_t        namelen,
                        uintmax_t     attr);
int com_fs_tmpfs_mkdir(com_vnode_t **out,
                       com_vnode_t  *parent,
                       const char   *name,
                       size_t        namelen,
                       uintmax_t     attr);
int com_fs_tmpfs_lookup(com_vnode_t **out,
                        com_vnode_t  *dir,
                        const char   *name,
                        size_t        len);
int com_fs_tmpfs_read(void        *buf,
                      size_t       buflen,
                      size_t      *bytes_read,
                      com_vnode_t *node,
                      uintmax_t    off,
                      uintmax_t    flags);
int com_fs_tmpfs_write(size_t      *bytes_written,
                       com_vnode_t *node,
                       void        *buf,
                       size_t       buflen,
                       uintmax_t    off,
                       uintmax_t    flags);
int com_fs_tmpfs_isatty(com_vnode_t *node);
int com_fs_tmpfs_stat(struct stat *out, com_vnode_t *node);

// OTHER FUNCTIONS

void  com_fs_tmpfs_set_other(com_vnode_t *vnode, void *data);
void *com_fs_tmpfs_get_other(com_vnode_t *vnode);
