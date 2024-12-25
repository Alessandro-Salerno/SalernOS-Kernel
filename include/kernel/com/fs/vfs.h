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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COM_VFS_PATH_SEP '/'

#define COM_VFS_CREAT_ATTR_GHOST 1

#define COM_FS_VFS_VNODE_HOLD(node) \
  __atomic_add_fetch(&node->num_ref, 1, __ATOMIC_SEQ_CST)
#define COM_FS_VFS_VNODE_RELEASE(node) \
  __atomic_add_fetch(&node->num_ref, -1, __ATOMIC_SEQ_CST)

typedef enum COM_FS_VFS_VNODE_type {
  COM_FS_VFS_VNODE_TYPE_FILE,
  COM_FS_VFS_VNODE_TYPE_DIR,
  COM_FS_VFS_VNODE_TYPE_LINK
} COM_FS_VFS_VNODE_type_t;

typedef struct com_vfs {
  struct com_vfs_ops *ops;
  struct COM_FS_VFS_VNODE   *mountpoint;
  struct COM_FS_VFS_VNODE   *root;
  void               *extra;
} com_vfs_t;

typedef struct COM_FS_VFS_VNODE {
  struct com_vfs       *mountpointof;
  struct com_vfs       *vfs;
  struct COM_FS_VFS_VNODE_ops *ops;
  struct {
    struct COM_FS_VFS_VNODE *next;
    struct COM_FS_VFS_VNODE *prev;
  } vlink;
  COM_FS_VFS_VNODE_type_t type;
  uintmax_t        num_ref;
  bool             isroot;
  void            *extra;
} COM_FS_VFS_VNODE_t;

typedef struct COM_FS_VFS_VNODE_ops {
  int (*close)(COM_FS_VFS_VNODE_t *vnode);
  int (*lookup)(COM_FS_VFS_VNODE_t **out,
                COM_FS_VFS_VNODE_t  *dir,
                const char   *name,
                size_t        len);
  int (*create)(COM_FS_VFS_VNODE_t **out,
                COM_FS_VFS_VNODE_t  *dir,
                const char   *name,
                size_t        namelen,
                uintmax_t     attr);
  int (*mkdir)(COM_FS_VFS_VNODE_t **out,
               COM_FS_VFS_VNODE_t  *parent,
               const char   *name,
               size_t        namelen,
               uintmax_t     attr);
  int (*link)(COM_FS_VFS_VNODE_t *dir,
              const char  *dstname,
              size_t       dstnamelen,
              COM_FS_VFS_VNODE_t *src);
  int (*unlink)(COM_FS_VFS_VNODE_t *dir, const char *name, size_t namelen);
  int (*read)(void        *buf,
              size_t       buflen,
              size_t      *bytes_read,
              COM_FS_VFS_VNODE_t *node,
              uintmax_t    off,
              uintmax_t    flags);
  int (*write)(size_t      *bytes_written,
               COM_FS_VFS_VNODE_t *node,
               void        *buf,
               size_t       buflen,
               uintmax_t    off,
               uintmax_t    flags);
  int (*resolve)(const char **path, size_t *pathlen, COM_FS_VFS_VNODE_t *link);
  int (*readdir)(void *buf, size_t *buflen, COM_FS_VFS_VNODE_t *dir, uintmax_t off);
  // TODO: add stat, mmap, munmap, getattr, setattr, poll, etc
  int (*ioctl)(COM_FS_VFS_VNODE_t *node, uintmax_t op, void *buf);
} COM_FS_VFS_VNODE_ops_t;

typedef struct com_vfs_ops {
  int (*mount)(com_vfs_t **out, COM_FS_VFS_VNODE_t *mountpoint);
  int (*unmount)(com_vfs_t *vfs);
  int (*vget)(COM_FS_VFS_VNODE_t **out, com_vfs_t *vfs, void *inode);
} com_vfs_ops_t;

void com_fs_vfs_vlink_set(COM_FS_VFS_VNODE_t *parent, COM_FS_VFS_VNODE_t *vlink);

int com_fs_vfs_close(COM_FS_VFS_VNODE_t *vnode);
int com_fs_vfs_lookup(COM_FS_VFS_VNODE_t **out,
                      const char   *path,
                      size_t        pathlen,
                      COM_FS_VFS_VNODE_t  *root,
                      COM_FS_VFS_VNODE_t  *cwd);
int com_fs_vfs_create(COM_FS_VFS_VNODE_t **out,
                      COM_FS_VFS_VNODE_t  *dir,
                      const char   *name,
                      size_t        namelen,
                      uintmax_t     attr);
int com_fs_vfs_mkdir(COM_FS_VFS_VNODE_t **out,
                     COM_FS_VFS_VNODE_t  *parent,
                     const char   *name,
                     size_t        namelen,
                     uintmax_t     attr);
int com_fs_vfs_link(COM_FS_VFS_VNODE_t *dir,
                    const char  *dstname,
                    size_t       dstnamelen,
                    COM_FS_VFS_VNODE_t *src);
int com_fs_vfs_unlink(COM_FS_VFS_VNODE_t *dir, const char *name, size_t namelen);
int com_fs_vfs_read(void        *buf,
                    size_t       buflen,
                    size_t      *bytes_read,
                    COM_FS_VFS_VNODE_t *node,
                    uintmax_t    off,
                    uintmax_t    flags);
int com_fs_vfs_write(size_t      *bytes_written,
                     COM_FS_VFS_VNODE_t *node,
                     void        *buf,
                     size_t       buflen,
                     uintmax_t    off,
                     uintmax_t    flags);
int com_fs_vfs_resolve(const char **path, size_t *pathlen, COM_FS_VFS_VNODE_t *link);
int com_fs_vfs_readdir(void        *buf,
                       size_t      *buflen,
                       COM_FS_VFS_VNODE_t *dir,
                       uintmax_t    off);
int com_fs_vfs_ioctl(COM_FS_VFS_VNODE_t *node, uintmax_t op, void *buf);
