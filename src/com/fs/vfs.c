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

#define GET_VLINK_CHILD(vn)          \
    while (NULL != vn->vlink.next) { \
        vn = vn->vlink.next;         \
    }

#define SKIP_SEPARATORS(path, end)                     \
    while (path != end && COM_VFS_PATH_SEP == *path) { \
        path++;                                        \
    }

void com_fs_vfs_vlink_set(com_vnode_t *parent, com_vnode_t *vlink) {
    if (NULL != parent) {
        if (NULL != parent->vlink.next) {
            parent->vlink.next->vlink.prev = NULL;
        }

        parent->vlink.next = vlink;
    }

    if (NULL != vlink) {
        vlink->vlink.prev = parent;
    }
}

int com_fs_vfs_close(com_vnode_t *vnode) {
    GET_VLINK_CHILD(vnode);

    if (NULL == vnode->ops->close) {
        return ENOSYS;
    }

    return vnode->ops->close(vnode);
}

int com_fs_vfs_lookup(com_vnode_t **out,
                      const char   *path,
                      size_t        pathlen,
                      com_vnode_t  *root,
                      com_vnode_t  *cwd) {
    com_vnode_t *ret     = cwd;
    const char  *pathend = path + pathlen;

    if (0 < pathlen && COM_VFS_PATH_SEP == path[0]) {
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
        section_end = kmemchr(path, COM_VFS_PATH_SEP, pathend - path);
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
            if (COM_VNODE_TYPE_DIR != ret->type) {
                COM_FS_VFS_VNODE_RELEASE(ret);
                *out = NULL;
                return ENOTDIR;
            }

            com_vnode_t *dir = ret;
            ret              = NULL;
            int fsret = dir->ops->lookup(&ret, dir, path, section_end - path);

            if (NULL == ret) {
                COM_FS_VFS_VNODE_RELEASE(dir);
                *out = NULL;
                return ENOENT;
            } else if (0 != fsret) {
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

            // TODO: handle case in which ret is symlink
        }

        path = section_end + 1;
    }

    *out = ret;
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

    return dir->ops->create(out, dir, name, namelen, attr);
}

int com_fs_vfs_mkdir(com_vnode_t **out,
                     com_vnode_t  *parent,
                     const char   *name,
                     size_t        namelen,
                     uintmax_t     attr) {
    if (NULL == parent->ops->mkdir) {
        return ENOSYS;
    }

    return parent->ops->mkdir(out, parent, name, namelen, attr);
}

int com_fs_vfs_link(com_vnode_t *dir,
                    const char  *dstname,
                    size_t       dstnamelen,
                    com_vnode_t *src) {
    if (NULL == src->ops->link) {
        return ENOSYS;
    }

    if (0 == src->ops->link(dir, dstname, dstnamelen, src)) {
        com_vnode_t *link = NULL;

        if (0 == dir->ops->lookup(&link, dir, dstname, dstnamelen) &&
            NULL != link) {
            link->vlink.prev = src->vlink.prev;
            link->vlink.next = src->vlink.next;
            COM_FS_VFS_VNODE_RELEASE(link);
            return 0;
        }
    }

    return ENOENT;
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
    GET_VLINK_CHILD(node);

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
    GET_VLINK_CHILD(node);

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

int com_fs_vfs_resolve(const char **path, size_t *pathlen, com_vnode_t *link) {
    if (NULL == link->ops->resolve) {
        return ENOSYS;
    }

    return link->ops->resolve(path, pathlen, link);
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
    GET_VLINK_CHILD(node);

    if (NULL == node->ops->ioctl) {
        return ENOSYS;
    }

    return node->ops->ioctl(node, op, buf);
}

int com_fs_vfs_isatty(com_vnode_t *node) {
    GET_VLINK_CHILD(node);

    if (NULL == node->ops->isatty) {
        return ENOSYS;
    }

    return node->ops->isatty(node);
}

int com_fs_vfs_stat(struct stat *out, com_vnode_t *node) {
    if (NULL == node->ops->stat) {
        return ENOSYS;
    }

    // TODO: use vlink, but needs to me masked
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
