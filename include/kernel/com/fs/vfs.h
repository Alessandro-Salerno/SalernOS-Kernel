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

#include <stddef.h>
#include <stdint.h>

#define COM_VNODE_HOLD(node) \
  __atomic_add_fetch(&node->num_ref, 1, __ATOMIC_SEQ_CST)
#define COM_VNODE_RELEASE(node) \
  __atomic_add_fetch(&node->num_ref, -1, __ATOMIC_SEQ_CST)

typedef enum com_vnode_type {
  COM_VNODE_TYPE_FILE,
  COM_VNODE_TYPE_DIR,
  COM_VNODE_TYPE_LINK
} com_vnode_type_t;

typedef struct com_vfs {
  struct com_vfs_ops *ops;
  struct com_vnode   *mountpoint;
  struct com_vnode   *root;
  void               *extra;
} com_vfs_t;

typedef struct com_vnode {
  struct com_vfs       *mountpointof;
  struct com_vfs       *vfs;
  struct com_vnode_ops *ops;
  com_vnode_type_t      type;
  uintmax_t             num_ref;
  void                 *extra;
} com_vnode_t;

typedef struct com_vnode_ops {
  int (*close)(com_vnode_t *vnode);
  int (*lookup)(com_vnode_t **out,
                com_vnode_t  *dir,
                const char   *name,
                size_t        len);
  int (*create)(com_vnode_t **out,
                com_vnode_t  *dir,
                const char   *name,
                size_t        namelen,
                uintmax_t     attr);
  int (*mkdir)(com_vnode_t **out,
               com_vnode_t  *parent,
               const char   *name,
               size_t        namelen,
               uintmax_t     attr);
  int (*link)(com_vnode_t *dir,
              const char  *dstname,
              size_t       dstnamelen,
              com_vnode_t *src);
  int (*unlink)(com_vnode_t *dir, const char *name, size_t namelen);
  int (*read)(void        *buf,
              size_t       buflen,
              com_vnode_t *node,
              uintmax_t    off,
              uintmax_t    flags);
  int (*write)(com_vnode_t *node,
               void        *buf,
               size_t       buflen,
               uintmax_t    off,
               uintmax_t    flags);
  int (*resolve)(const char **path, size_t *pathlen, com_vnode_t *link);
  int (*readdir)(void *buf, size_t *buflen, com_vnode_t *dir, uintmax_t off);
  // TODO: add stat, mmap, munmap, getattr, setattr, poll, etc
  int (*ioctl)(com_vnode_t *node, uintmax_t op, void *buf);
} com_vnode_ops_t;

typedef struct com_vfs_ops {
  void (*mount)(com_vfs_t **out, com_vnode_t *mountpoint);
  void (*unmount)(com_vfs_t *vfs);
  void (*vget)(com_vnode_t **out, com_vfs_t *vfs, void *inode);
} com_vfs_ops_t;
