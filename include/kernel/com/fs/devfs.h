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

typedef struct com_dev_ops {
    com_vnode_type_t type; // CHARDEV or BLOCKDEV
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
    int (*seekable)(void *devdata);
    int (*stat)(struct stat *out, void *devdata);
    int (*poll_head)(struct com_poll_head **out, void *devdata);
    int (*poll)(short *revents, void *devdata, short events);
    int (*mmap)(void             **out,
                void              *devdata,
                com_vmm_context_t *vmm_context,
                void              *hint,
                size_t             size,
                int                vmm_flags,
                arch_mmu_flags_t   mmu_flags,
                uintmax_t          off);

    int (*open)(com_vnode_t **out, void *devdata);
    int (*close)(void *devdata);
    void (*inactive)(void *devdata);
} com_dev_ops_t;

int   com_fs_devfs_register(com_vnode_t  **out,
                            com_vnode_t   *dir,
                            const char    *name,
                            size_t         namelen,
                            com_dev_ops_t *devops,
                            void          *devdata);
int   com_fs_devfs_register_anonymous(com_vnode_t  **out,
                                      com_dev_ops_t *devops,
                                      void          *devdata);
int   com_fs_devfs_mkdir(com_vnode_t **out,
                         com_vnode_t  *parent,
                         const char   *name,
                         size_t        namelen);
void *com_fs_devfs_get_data(com_vnode_t *node);
int   com_fs_devfs_init(com_vfs_t **out, com_vfs_t *rootfs);
