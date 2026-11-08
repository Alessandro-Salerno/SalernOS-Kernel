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

typedef struct com_fs_tmpfs_special {
    com_vnode_ops_t *ops;
    void *private;
} com_fs_tmpfs_special_t;

void com_fs_tmpfs_init(void);
int  com_fs_tmpfs_mount(com_vfs_t **out, com_vnode_t *mountpoint);

int com_fs_tmpfs_new_orphan(com_vnode_t           **out,
                            com_vfs_t              *vfs,
                            const com_vnode_attr_t *attr);

void   *com_fs_tmpfs_private(com_vnode_t *vnode);
nlink_t com_fs_tmpfs_nlink(com_vnode_t *vnode);
void    com_fs_tmpfs_inactive(com_vnode_t *vnode);
