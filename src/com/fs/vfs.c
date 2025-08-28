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

#include <errno.h>
#include <kernel/com/fs/vfs.h>
#include <lib/mem.h>
#include <lib/printf.h>
#include <lib/util.h>
#include <stdbool.h>
#include <stdlib.h>

#define SKIP_SEPARATORS(path, end)        \
    while (path != end && '/' == *path) { \
        path++;                           \
    }

static int vfs_lookup1(com_vnode_t **out,
                       com_vnode_t **old_dir,
                       const char   *path,
                       size_t        pathlen,
                       com_vnode_t  *root,
                       com_vnode_t  *cwd) {
    com_vnode_t *ret     = cwd;
    const char  *pathend = path + pathlen;

    if (pathlen > 0 && '/' == path[0]) {
        path++;
        ret = root;
    } else if (NULL == cwd) {
        return EINVAL;
    }

    SKIP_SEPARATORS(path, pathend);
    COM_FS_VFS_VNODE_HOLD(ret);

    const char *section_end = NULL;
    bool        end_reached = false;
    while (path < pathend && !end_reached) {
        section_end = kmemchr(path, '/', pathend - path);
        end_reached = NULL == section_end;

        if (end_reached) {
            section_end = pathend;
        }

        bool dot    = 1 == section_end - path && 0 == kmemcmp(path, ".", 1);
        bool dotdot = 2 == section_end - path && 0 == kmemcmp(path, "..", 2);

        while (dotdot && ret->isroot && NULL != ret->vfs->mountpoint) {
            com_vnode_t *old = ret;
            ret              = ret->vfs->mountpoint;
            COM_FS_VFS_VNODE_RELEASE(old);
            COM_FS_VFS_VNODE_HOLD(ret);
        }

        if (!dot) {
            if (E_COM_VNODE_TYPE_DIR != ret->type) {
                COM_FS_VFS_VNODE_RELEASE(ret);
                *out = NULL;
                return ENOTDIR;
            }

            com_vnode_t *dir = ret;
            ret              = NULL;
            int fsret = dir->ops->lookup(&ret, dir, path, section_end - path);

            if (0 != fsret) {
                COM_FS_VFS_VNODE_RELEASE(dir);
                *out = NULL;
                return fsret;
            }

            while (NULL != ret->mountpointof) {
                if (dir != ret) {
                    COM_FS_VFS_VNODE_RELEASE(dir);
                }

                dir = ret;
                ret = ret->mountpointof->root;
                COM_FS_VFS_VNODE_HOLD(ret);
            }

            if (E_COM_VNODE_TYPE_LINK == ret->type) {
                *out     = ret;
                *old_dir = dir;
                return 0;
            }
        }

        path = section_end + 1;
    }

    *out = ret;
    return 0;
}

int com_fs_vfs_close(com_vnode_t *vnode) {
    if (NULL == vnode->ops->close) {
        return ENOSYS;
    }

    return vnode->ops->close(vnode);
}

int com_fs_vfs_lookup(com_vnode_t **out,
                      const char   *path,
                      size_t        pathlen,
                      com_vnode_t  *root,
                      com_vnode_t  *cwd,
                      bool          follow_symlinks) {
    com_vnode_t *curr = NULL;
    com_vnode_t *prev = NULL;

    for (size_t i = 0;; i++) {
        // Here prev == curr
        // Here (curr = prev) is held (Nth iteration) or NULL (first iteration)
        KASSERT(prev == curr);
        if (CONFIG_SYMLINK_MAX == i) {
            COM_FS_VFS_VNODE_RELEASE(prev);
            *out = NULL;
            return ELOOP;
        }

        int ret = vfs_lookup1(&curr, &cwd, path, pathlen, root, cwd);
        if (0 != ret) {
            *out = NULL;
            return ret;
        }

        // Here prev != curr
        // Here curr is always held
        // Here prev is either held (Nth iteration) or NULL (first iteration)
        KASSERT(prev != curr);
        if (E_COM_VNODE_TYPE_LINK != curr->type) {
            break;
        }

        // Here curr != NULL
        // Here prev != curr
        // Control reaches here only if curr is a symlink
        KASSERT(prev != curr);
        ret = com_fs_vfs_readlink(&path, &pathlen, curr);
        if (0 != ret) {
            COM_FS_VFS_VNODE_RELEASE(prev);
            COM_FS_VFS_VNODE_RELEASE(curr);
            *out = NULL;
            return ret;
        }

        // Here prev is either held (Nth iteration) or NULL (first iteration)
        // Here curr is always held
        COM_FS_VFS_VNODE_RELEASE(prev);
        // Here prev is no longer held
        prev = curr;
        // Here prev = curr, thus prev is a symlink and is held
    }

    // Here curr != NULL because we can only get here with the break, which also
    // implies taht curr is not a symlink
    // Here prev != curr because we can only get here with the break
    // Here prev is either held (Nth iteration) or NULL (first iteration)
    KASSERT(E_COM_VNODE_TYPE_LINK != curr->type);

    if (NULL == prev) {
        // Here we know the loop only ran once, so there was no symlink involved
        *out = curr;
        return 0;
    }

    // Control only reaches here if the previous if branch wasn't taken, so here
    // we know that prev != NULL and is a symlink
    KASSERT(E_COM_VNODE_TYPE_LINK == prev->type);

    if (follow_symlinks) {
        COM_FS_VFS_VNODE_RELEASE(prev);
        *out = curr;
        return 0;
    }

    // Control only reaches here if the previous if branch wasn't taken, so here
    // we know that the caller didn't want to follow the last symlink
    COM_FS_VFS_VNODE_RELEASE(curr);
    *out = prev;
    return 0;
}

int com_fs_vfs_create(com_vnode_t **out,
                      com_vnode_t  *dir,
                      const char   *name,
                      size_t        namelen,
                      uintmax_t     attr) {
    if (NULL == dir->ops->create) {
        return ENOSYS;
    }

    return dir->ops->create(out, dir, name, namelen, attr, 0);
}

int com_fs_vfs_mkdir(com_vnode_t **out,
                     com_vnode_t  *parent,
                     const char   *name,
                     size_t        namelen,
                     uintmax_t     attr) {
    if (NULL == parent->ops->mkdir) {
        return ENOSYS;
    }

    return parent->ops->mkdir(out, parent, name, namelen, attr, 0);
}

int com_fs_vfs_link(com_vnode_t *dir,
                    const char  *dstname,
                    size_t       dstnamelen,
                    com_vnode_t *src) {
    if (NULL == src->ops->link) {
        return ENOSYS;
    }

    return src->ops->link(dir, dstname, dstnamelen, src);
}

int com_fs_vfs_unlink(com_vnode_t *dir, const char *name, size_t namelen) {
    if (NULL == dir->ops->unlink) {
        return ENOSYS;
    }

    return dir->ops->unlink(dir, name, namelen);
}

int com_fs_vfs_read(void        *buf,
                    size_t       buflen,
                    size_t      *bytes_read,
                    com_vnode_t *node,
                    uintmax_t    off,
                    uintmax_t    flags) {
    if (NULL == node->ops->read) {
        return ENOSYS;
    }

    size_t dump = 0;
    if (NULL == bytes_read) {
        bytes_read = &dump;
    }

    *bytes_read = 0;
    return node->ops->read(buf, buflen, bytes_read, node, off, flags);
}

int com_fs_vfs_write(size_t      *bytes_written,
                     com_vnode_t *node,
                     void        *buf,
                     size_t       buflen,
                     uintmax_t    off,
                     uintmax_t    flags) {
    if (NULL == node->ops->write) {
        return ENOSYS;
    }

    size_t dump = 0;
    if (NULL == bytes_written) {
        bytes_written = &dump;
    }

    *bytes_written = 0;
    return node->ops->write(bytes_written, node, buf, buflen, off, flags);
}

int com_fs_vfs_symlink(com_vnode_t *dir,
                       const char  *linkname,
                       size_t       linknamelen,
                       const char  *path,
                       size_t       pathlen) {
    if (NULL == dir->ops->symlink) {
        return ENOSYS;
    }

    return dir->ops->symlink(dir, linkname, linknamelen, path, pathlen);
}

int com_fs_vfs_readlink(const char **path, size_t *pathlen, com_vnode_t *link) {
    if (NULL == link->ops->readlink) {
        return ENOSYS;
    }

    return link->ops->readlink(path, pathlen, link);
}

int com_fs_vfs_readdir(void        *buf,
                       size_t       buflen,
                       size_t      *bytes_read,
                       com_vnode_t *dir,
                       uintmax_t    off) {
    if (NULL == dir->ops->readdir) {
        return ENOSYS;
    }

    return dir->ops->readdir(buf, buflen, bytes_read, dir, off);
}

int com_fs_vfs_ioctl(com_vnode_t *node, uintmax_t op, void *buf) {
    if (NULL == node->ops->ioctl) {
        return ENOSYS;
    }

    return node->ops->ioctl(node, op, buf);
}

int com_fs_vfs_isatty(com_vnode_t *node) {
    if (NULL == node->ops->isatty) {
        return ENOSYS;
    }

    return node->ops->isatty(node);
}

int com_fs_vfs_stat(struct stat *out, com_vnode_t *node) {
    if (NULL == node->ops->stat) {
        return ENOSYS;
    }

    kmemset(out, sizeof(struct stat), 0);
    return node->ops->stat(out, node);
}

int com_fs_vfs_truncate(com_vnode_t *node, size_t size) {
    if (NULL == node->ops->truncate) {
        return ENOSYS;
    }

    return node->ops->truncate(node, size);
}

int com_fs_vfs_vnctl(com_vnode_t *node, uintmax_t op, void *buf) {
    if (NULL == node->ops->vnctl) {
        return ENOSYS;
    }

    return node->ops->vnctl(node, op, buf);
}

int com_fs_vfs_poll_head(struct com_poll_head **out, com_vnode_t *node) {
    if (NULL == node->ops->poll_head) {
        return ENOSYS;
    }

    return node->ops->poll_head(out, node);
}

int com_fs_vfs_poll(short *revents, com_vnode_t *node, short events) {
    if (NULL == node->ops->poll) {
        return ENOSYS;
    }

    return node->ops->poll(revents, node, events);
}

int com_fs_vfs_open(com_vnode_t **out, com_vnode_t *node) {
    if (NULL == node->ops->open) {
        *out = node;
        return 0;
    }

    return node->ops->open(out, node);
}
