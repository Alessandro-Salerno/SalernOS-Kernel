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
#include <kernel/com/mm/slab.h>
#include <stdint.h>

#define COM_FS_FILE_HOLD(file) \
    __atomic_add_fetch(&file->num_ref, 1, __ATOMIC_SEQ_CST)

#define COM_FS_FILE_RELEASE(file)                                         \
    ({                                                                    \
        if (NULL != file->vnode) {                                        \
            uintmax_t n =                                                 \
                __atomic_add_fetch(&file->num_ref, -1, __ATOMIC_SEQ_CST); \
            if (0 == n) {                                                 \
                COM_FS_VFS_VNODE_RELEASE(file->vnode);                    \
                com_mm_slab_free(file, sizeof(com_file_t));               \
                file = NULL;                                              \
            }                                                             \
        }                                                                 \
    })

typedef struct com_file {
    // TODO: this is
    // a lock but I
    // have circular
    // dependencies
    // if I include
    // spinlock.h
    int       off_lock;
    uintmax_t off;
    uintmax_t flags;
    uintmax_t num_ref; // this is used because multiple file descripts may point
                       // to the same file (e.g., if stdout and stderr and the
                       // same) or if the process was forked
    com_vnode_t *vnode;
} com_file_t;

typedef struct com_filedesc {
    com_file_t *file;
    uintmax_t   flags;
} com_filedesc_t;
