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

int com_fs_pipefs_read(void        *buf,
                       size_t       buflen,
                       size_t      *bytes_read,
                       COM_FS_VFS_VNODE_t *node,
                       uintmax_t    off,
                       uintmax_t    flags);
int com_fs_pipefs_write(size_t      *bytes_written,
                        COM_FS_VFS_VNODE_t *node,
                        void        *buf,
                        size_t       buflen,
                        uintmax_t    off,
                        uintmax_t    flags);
int com_fs_pipefs_close(COM_FS_VFS_VNODE_t *vnode);

void com_fs_pipefs_new(COM_FS_VFS_VNODE_t **read, COM_FS_VFS_VNODE_t **write);
