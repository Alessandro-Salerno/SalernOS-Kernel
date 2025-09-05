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

#include <lib/ioviter.h>
#include <lib/spinlock.h>
#include <lib/util.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <vendor/tailq.h>

// NOTE: Forward declaration, definition in <kernel/com/fs/poll.h>
// It doesn't like it if I include it here
struct com_poll_head;

#define COM_FS_VFS_VNCTL_GETNAME 1

#define COM_FS_VFS_VNODE_HOLD(node) \
    __atomic_add_fetch(&node->num_ref, 1, __ATOMIC_SEQ_CST)
#define COM_FS_VFS_VNODE_RELEASE(node)                                   \
    if (NULL != (node) &&                                                \
        0 == __atomic_add_fetch(&node->num_ref, -1, __ATOMIC_SEQ_CST)) { \
        KDEBUG("freeing vnode %p", (node));                              \
        com_fs_vfs_close(node);                                          \
    }

typedef enum com_vnode_type {
    E_COM_VNODE_TYPE_FILE,
    E_COM_VNODE_TYPE_DIR,
    E_COM_VNODE_TYPE_LINK,
    E_COM_VNODE_TYPE_CHARDEV,
    E_COM_VNODE_TYPE_FIFO,
    E_COM_VNODE_TYPE_SOCKET,
    E_COM_VNODE_TYPE_BLOCKDEV
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
    bool                  isroot;
    void                 *extra;
    void                 *binding; // Used for socket/FIFO
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
                  uintmax_t     attr,
                  uintmax_t     fsattr);
    int (*mkdir)(com_vnode_t **out,
                 com_vnode_t  *parent,
                 const char   *name,
                 size_t        namelen,
                 uintmax_t     attr,
                 uintmax_t     fsattr);
    int (*link)(com_vnode_t *dir,
                const char  *dstname,
                size_t       dstnamelen,
                com_vnode_t *src);
    int (*unlink)(com_vnode_t *node, int flags);
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
    int (*readv)(kioviter_t  *ioviter,
                 size_t      *bytes_read,
                 com_vnode_t *node,
                 uintmax_t    off,
                 uintmax_t    flags);
    int (*writev)(size_t      *bytes_written,
                  com_vnode_t *node,
                  kioviter_t  *ioviter,
                  uintmax_t    off,
                  uintmax_t    flags);
    int (*symlink)(com_vnode_t *dir,
                   const char  *linkname,
                   size_t       linknamelen,
                   const char  *path,
                   size_t       pathlen);
    int (*readlink)(const char **path, size_t *pathlen, com_vnode_t *link);
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
    int (*poll_head)(struct com_poll_head **out, com_vnode_t *node);
    int (*poll)(short *revents, com_vnode_t *node, short events);
    int (*open)(com_vnode_t **out, com_vnode_t *node);
    int (*mksocket)(com_vnode_t **out,
                    com_vnode_t  *dir,
                    const char   *name,
                    size_t        namelen,
                    uintmax_t     attr,
                    uintmax_t     fsattr);
    int (*mmap)(void       **out,
                com_vnode_t *node,
                uintptr_t    hint,
                size_t       size,
                uintmax_t    flags,
                uintmax_t    prot,
                uintmax_t    off);
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

int com_fs_vfs_close(com_vnode_t *vnode);
int com_fs_vfs_lookup(com_vnode_t **out,
                      const char   *path,
                      size_t        pathlen,
                      com_vnode_t  *root,
                      com_vnode_t  *cwd,
                      bool          follow_symlinks);
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
int com_fs_vfs_unlink(com_vnode_t *node, int flags);
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
int com_fs_vfs_readv(kioviter_t  *ioviter,
                     size_t      *bytes_read,
                     com_vnode_t *node,
                     uintmax_t    off,
                     uintmax_t    flags);
int com_fs_vfs_writev(size_t      *bytes_written,
                      com_vnode_t *node,
                      kioviter_t  *ioviter,
                      uintmax_t    off,
                      uintmax_t    flags);
int com_fs_vfs_symlink(com_vnode_t *dir,
                       const char  *linkname,
                       size_t       linknamelen,
                       const char  *path,
                       size_t       pathlen);
int com_fs_vfs_readlink(const char **path, size_t *pathlen, com_vnode_t *link);
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
int com_fs_vfs_poll_head(struct com_poll_head **out, com_vnode_t *node);
int com_fs_vfs_poll(short *revents, com_vnode_t *node, short events);
int com_fs_vfs_open(com_vnode_t **out, com_vnode_t *node);
int com_fs_vfs_mksocket(com_vnode_t **out,
                        com_vnode_t  *dir,
                        const char   *name,
                        size_t        namelen,
                        uintmax_t     attr);
int com_fs_vfs_mmap(void       **out,
                    com_vnode_t *node,
                    uintptr_t    hint,
                    size_t       size,
                    uintmax_t    flags,
                    uintmax_t    prot,
                    uintmax_t    off);

// UTILITY FUNCTIONS

int com_fs_vfs_alloc_vnode(com_vnode_t    **out,
                           com_vfs_t       *vfs,
                           com_vnode_type_t type,
                           com_vnode_ops_t *ops,
                           void            *extra);
int com_fs_vfs_create_any(com_vnode_t **out,
                          const char   *path,
                          size_t        pathlen,
                          com_vnode_t  *root,
                          com_vnode_t  *cwd,
                          uintmax_t     attr,
                          bool          err_if_present,
                          bool          follow_symlinks,
                          int (*create_handler)(com_vnode_t **out,
                                                com_vnode_t  *dir,
                                                const char   *name,
                                                size_t        namelen,
                                                uintmax_t     attr));
