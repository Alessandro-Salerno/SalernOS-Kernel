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

#include <lib/util.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

#define COM_VFS_PATH_SEP '/'

#define COM_VFS_CREAT_ATTR_GHOST 1

#define COM_FS_VFS_VNCTL_GETNAME 1

#define COM_FS_VFS_VNODE_HOLD(node) \
    __atomic_add_fetch(&node->num_ref, 1, __ATOMIC_SEQ_CST)
#define COM_FS_VFS_VNODE_RELEASE(node)                                   \
    if (NULL != (node) &&                                                \
        0 == __atomic_add_fetch(&node->num_ref, -1, __ATOMIC_SEQ_CST)) { \
        KDEBUG("freeing vnode %x", (node));                              \
        com_fs_vfs_close(node);                                          \
    }

typedef enum com_vnode_type {
    COM_VNODE_TYPE_FILE,
    COM_VNODE_TYPE_DIR,
    COM_VNODE_TYPE_LINK
} com_vnode_type_t;

typedef struct com_vfs {
    struct com_vfs_ops      *ops;
    struct com_fs_vfs_vnode *mountpoint;
    struct com_fs_vfs_vnode *root;
    void                    *extra;
} com_vfs_t;

typedef struct com_fs_vfs_vnode {
    struct com_vfs              *mountpointof;
    struct com_vfs              *vfs;
    struct com_fs_vfs_vnode_ops *ops;
    struct {
        struct com_fs_vfs_vnode *next;
        struct com_fs_vfs_vnode *prev;
    } vlink;
    com_vnode_type_t type;
    uintmax_t        num_ref;
    bool             isroot;
    void            *extra;
} com_vnode_t;

typedef struct com_fs_vfs_vnode_ops {
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
                size_t      *bytes_read,
                com_vnode_t *node,
                uintmax_t    off,
                uintmax_t    flags);
    int (*write)(size_t      *bytes_written,
                 com_vnode_t *node,
                 void        *buf,
                 size_t       buflen,
                 uintmax_t    off,
                 uintmax_t    flags);
    int (*resolve)(const char **path, size_t *pathlen, com_vnode_t *link);
    int (*readdir)(void        *buf,
                   size_t       buflen,
                   size_t      *bytes_read,
                   com_vnode_t *dir,
                   uintmax_t    off);
    int (*ioctl)(com_vnode_t *node, uintmax_t op, void *buf);
    int (*isatty)(com_vnode_t *node);
    int (*stat)(struct stat *out, com_vnode_t *node);
    int (*truncate)(com_vnode_t *node, size_t size);
    int (*vnctl)(com_vnode_t *node, uintmax_t op, void *buf);
} com_vnode_ops_t;

typedef struct com_vfs_ops {
    int (*mount)(com_vfs_t **out, com_vnode_t *mountpoint);
    int (*unmount)(com_vfs_t *vfs);
    int (*vget)(com_vnode_t **out, com_vfs_t *vfs, void *inode);
} com_vfs_ops_t;

typedef struct com_dirent {
    ino_t          ino;
    off_t          off;
    unsigned short reclen;
    unsigned char  type;
    char           name[];
} com_dirent_t;

typedef struct com_vnctl_name {
    const char *name;
    size_t      namelen;
} com_vnctl_name_t;

void com_fs_vfs_vlink_set(com_vnode_t *parent, com_vnode_t *vlink);

int com_fs_vfs_close(com_vnode_t *vnode);
int com_fs_vfs_lookup(com_vnode_t **out,
                      const char   *path,
                      size_t        pathlen,
                      com_vnode_t  *root,
                      com_vnode_t  *cwd);
int com_fs_vfs_create(com_vnode_t **out,
                      com_vnode_t  *dir,
                      const char   *name,
                      size_t        namelen,
                      uintmax_t     attr);
int com_fs_vfs_mkdir(com_vnode_t **out,
                     com_vnode_t  *parent,
                     const char   *name,
                     size_t        namelen,
                     uintmax_t     attr);
int com_fs_vfs_link(com_vnode_t *dir,
                    const char  *dstname,
                    size_t       dstnamelen,
                    com_vnode_t *src);
int com_fs_vfs_unlink(com_vnode_t *dir, const char *name, size_t namelen);
int com_fs_vfs_read(void        *buf,
                    size_t       buflen,
                    size_t      *bytes_read,
                    com_vnode_t *node,
                    uintmax_t    off,
                    uintmax_t    flags);
int com_fs_vfs_write(size_t      *bytes_written,
                     com_vnode_t *node,
                     void        *buf,
                     size_t       buflen,
                     uintmax_t    off,
                     uintmax_t    flags);
int com_fs_vfs_resolve(const char **path, size_t *pathlen, com_vnode_t *link);
int com_fs_vfs_readdir(void        *buf,
                       size_t       buflen,
                       size_t      *bytes_read,
                       com_vnode_t *dir,
                       uintmax_t    off);
int com_fs_vfs_ioctl(com_vnode_t *node, uintmax_t op, void *buf);
int com_fs_vfs_isatty(com_vnode_t *node);
int com_fs_vfs_stat(struct stat *out, com_vnode_t *node);
int com_fs_vfs_truncate(com_vnode_t *node, size_t size);
int com_fs_vfs_vnctl(com_vnode_t *node, uintmax_t op, void *buf);
