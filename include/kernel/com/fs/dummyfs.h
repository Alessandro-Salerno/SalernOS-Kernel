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

// NOTE: this is a dummy file system written to test the VFS
// it is not meant to be used or taken as an example
// it's here because I don't want to lete the code for now

#pragma once

#include <arch/info.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/log.h>

// VFS OPS

int com_fs_dummyfs_vget(COM_FS_VFS_VNODE_t **out, com_vfs_t *vfs, void *inode);
int com_fs_dummyfs_mount(com_vfs_t **out, COM_FS_VFS_VNODE_t *mountpoint);

// VNODE OPS

int com_fs_dummyfs_create(COM_FS_VFS_VNODE_t **out,
                          COM_FS_VFS_VNODE_t  *dir,
                          const char   *name,
                          size_t        namelen,
                          uintmax_t     attr);
int com_fs_dummyfs_write(size_t      *bytes_written,
                         COM_FS_VFS_VNODE_t *node,
                         void        *buf,
                         size_t       buflen,
                         uintmax_t    off,
                         uintmax_t    flags);
int com_fs_dummyfs_read(void        *buf,
                        size_t       buflen,
                        size_t      *bytes_read,
                        COM_FS_VFS_VNODE_t *node,
                        uintmax_t    off,
                        uintmax_t    flags);
int com_fs_dummyfs_lookup(COM_FS_VFS_VNODE_t **out,
                          COM_FS_VFS_VNODE_t  *dir,
                          const char   *name,
                          size_t        len);
