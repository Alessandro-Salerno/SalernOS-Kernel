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
#include <kernel/com/fs/sockfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/slab.h>
#include <lib/mem.h>
#include <lib/printf.h>
#include <lib/str.h>
#include <lib/util.h>
#include <stdbool.h>
#include <stdlib.h>

#define SKIP_SEPARATORS(path, end)        \
    while (path != end && '/' == *path) { \
        path++;                           \
    }

// out is the oujtput vnode, and is always set (NULL on errors, valid vnode
// otherwise) out_dir is the directory containing out, which is set only if out
// is a symlink, otherwise it's undefined. out_subpath and out_subpathlen are
// always updated. If out is a symlink, they will point to the sybpath to
// explore after resolving the symlink (if the symlink points to a directory),
// they're NULL and 0 respectivelly otherwise
static int vfs_lookup1(com_vnode_t **out,
                       com_vnode_t **out_dir,
                       const char  **out_subpath,
                       size_t       *out_subpathlen,
                       const char   *path,
                       size_t        pathlen,
                       com_vnode_t  *root,
                       com_vnode_t  *cwd) {
    com_vnode_t *ret_vn         = cwd;
    int          ret            = 0;
    const char  *ret_subpath    = NULL;
    size_t       ret_subpathlen = 0;
    const char  *pathend        = path + pathlen;

    if (pathlen > 0 && '/' == path[0]) {
        path++;
        ret_vn = root;
    } else if (NULL == cwd) {
        return EINVAL;
    }

    COM_FS_VFS_VNODE_HOLD(ret_vn);

    const char *section_end = NULL;
    bool        end_reached = false;
    while (path < pathend && !end_reached) {
        SKIP_SEPARATORS(path, pathend);
        if (path == pathend) {
            break;
        }

        section_end = kmemchr(path, '/', pathend - path);
        end_reached = NULL == section_end;
        if (end_reached) {
            section_end = pathend;
        }

        bool dot    = 1 == section_end - path && 0 == kmemcmp(path, ".", 1);
        bool dotdot = 2 == section_end - path && 0 == kmemcmp(path, "..", 2);

        while (dotdot && ret_vn->isroot && NULL != ret_vn->vfs->mountpoint) {
            com_vnode_t *old = ret_vn;
            ret_vn           = ret_vn->vfs->mountpoint;
            COM_FS_VFS_VNODE_RELEASE(old);
            COM_FS_VFS_VNODE_HOLD(ret_vn);
        }

        if (!dot) {
            if (E_COM_VNODE_TYPE_DIR != ret_vn->type) {
                COM_FS_VFS_VNODE_RELEASE(ret_vn);
                ret_vn = NULL;
                ret    = ENOTDIR;
                break;
            }

            com_vnode_t *dir = ret_vn;
            ret_vn           = NULL;
            ret = dir->ops->lookup(&ret_vn, dir, path, section_end - path);
            if (0 != ret) {
                COM_FS_VFS_VNODE_RELEASE(dir);
                ret_vn = NULL;
                break;
            }

            while (NULL != ret_vn->mountpointof) {
                if (dir != ret_vn) {
                    COM_FS_VFS_VNODE_RELEASE(dir);
                }

                dir    = ret_vn;
                ret_vn = ret_vn->mountpointof->root;
                COM_FS_VFS_VNODE_HOLD(ret_vn);
            }

            if (E_COM_VNODE_TYPE_LINK == ret_vn->type) {
                *out_dir = dir;
                if (NULL != section_end && !end_reached) {
                    ret_subpath    = section_end;
                    ret_subpathlen = pathend - section_end;
                }
                break;
            }
        }

        path = section_end + 1;
    }

    if (NULL != out_subpath) {
        *out_subpath = ret_subpath;
    }
    if (NULL != out_subpathlen) {
        *out_subpathlen = ret_subpathlen;
    }

    *out = ret_vn;
    return ret;
}

static int vfs_lookup2(com_vnode_t **out,
                       com_vnode_t  *root,
                       com_vnode_t  *cwd,
                       com_vnode_t  *link) {
    const char *path    = NULL;
    size_t      pathlen = 0;

    for (size_t i = 0; i < CONFIG_SYMLINK_MAX; i++) {
        if (E_COM_VNODE_TYPE_LINK != link->type) {
            *out = link;
            return 0;
        }

        int ret = com_fs_vfs_readlink(&path, &pathlen, link);
        // Release the vnode as soon as possible: this ensures that, regardlless
        // of whether the operations fails or succeeds, the caller can expect
        // link to be released if it is a symlink
        COM_FS_VFS_VNODE_RELEASE(link);
        if (0 != ret) {
            *out = NULL;
            return ret;
        }

        ret = vfs_lookup1(&link, &cwd, NULL, NULL, path, pathlen, root, cwd);
        if (0 != ret) {
            *out = NULL;
            return ret;
        }
    }

    *out = NULL;
    COM_FS_VFS_VNODE_RELEASE(link);
    return ELOOP;
}

int com_fs_vfs_close(com_vnode_t *vnode) {
    if (NULL == vnode->ops->close) {
        return ENOSYS;
    }

    return vnode->ops->close(vnode);
}

// NOTE: Maybe engineered, but works and I coun't come up with a simpler way
int com_fs_vfs_lookup(com_vnode_t **out,
                      const char   *path,
                      size_t        pathlen,
                      com_vnode_t  *root,
                      com_vnode_t  *cwd,
                      bool          follow_symlinks) {
    com_vnode_t *tmp_out = NULL;
    int          ret     = 0;

    do {
        // vfs_lookup1 shall be called at least once per lookup (thus do-while)
        // If it fails or gets to the end of the path, the loop wil exit
        // (explained below)
        com_vnode_t *symlink_dir;
        ret = vfs_lookup1(&tmp_out,
                          &symlink_dir,
                          &path,
                          &pathlen,
                          path,
                          pathlen,
                          root,
                          cwd);
        if (0 != ret) {
            *out = NULL;
            goto end;
        }

        // Control reaches here if vfs_lookup1 is successful, the we know that
        // (NULL == path) => (0 == pathlen)
        // So if NULL != path, then vfs_lookup1 has returned preemptively due to
        // it meeting a symlink. However, if NULL == path, the state of
        // tmp_out->type is unknown: it could be a normal node or a symlink,
        // either way the end of the path has been reached so either:
        // 1. It is a symlink that needs resolution given the value of
        // follow_symlinks
        // 2. tmp_out->type and/or follow_symlinks impose not to resolve it
        // Given the implication above, the loop will exit automatically if
        // needed
        if (NULL != path ||
            (E_COM_VNODE_TYPE_LINK == tmp_out->type && follow_symlinks)) {
            ret = vfs_lookup2(&tmp_out, root, symlink_dir, tmp_out);
            if (0 != ret) {
                *out = NULL;
                goto end;
            }

            // If the symlink was the last component of the path, then this is
            // just going to be discarded. However, if it was an intermediate
            // component, we should start the new lookup using it as the working
            // directory and using the previous subpath as the path. We also set
            // root = tmp_out to avoid issues with paths that have multiple /
            cwd  = tmp_out;
            root = tmp_out;
        }
    } while (pathlen > 0);

    *out = tmp_out;
end:
    return ret;
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

int com_fs_vfs_unlink(com_vnode_t *node, int flags) {
    if (NULL == node->ops->unlink) {
        return ENOSYS;
    }

    return node->ops->unlink(node, flags);
}

int com_fs_vfs_read(void        *buf,
                    size_t       buflen,
                    size_t      *bytes_read,
                    com_vnode_t *node,
                    uintmax_t    off,
                    uintmax_t    flags) {
    size_t dump = 0;
    if (NULL == bytes_read) {
        bytes_read = &dump;
    }

    if (NULL == node->ops->read) {
        if (NULL != node->ops->readv) {
            struct iovec iov = {.iov_base = buf, .iov_len = buflen};
            kioviter_t   iter;
            kioviter_init(&iter, &iov, 1);
            return node->ops->readv(&iter, bytes_read, node, off, flags);
        }

        return ENOSYS;
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
    size_t dump = 0;
    if (NULL == bytes_written) {
        bytes_written = &dump;
    }

    if (NULL == node->ops->write) {
        if (NULL != node->ops->writev) {
            struct iovec iov = {.iov_base = buf, .iov_len = buflen};
            kioviter_t   iter;
            kioviter_init(&iter, &iov, 1);
            return node->ops->writev(bytes_written, node, &iter, off, flags);
        }

        return ENOSYS;
    }

    *bytes_written = 0;
    return node->ops->write(bytes_written, node, buf, buflen, off, flags);
}

int com_fs_vfs_readv(kioviter_t  *ioviter,
                     size_t      *bytes_read,
                     com_vnode_t *node,
                     uintmax_t    off,
                     uintmax_t    flags) {
    size_t dump = 0;
    if (NULL == bytes_read) {
        bytes_read = &dump;
    }

    if (NULL != node->ops->readv) {
        return node->ops->readv(ioviter, bytes_read, node, off, flags);
    }

    // If the node does not support iovec, try to stub it with sequential
    // accesses
    if (NULL == node->ops->read) {
        return ENOSYS;
    }

    struct iovec *iov;
    int           ret      = 0;
    size_t        tot_read = 0;

    for (size_t iov_off = off;
         NULL != (iov = kioviter_next(ioviter)) && 0 == ret;) {
        size_t curr_read = 0;
        ret              = node->ops->read(iov->iov_base,
                              iov->iov_len,
                              &curr_read,
                              node,
                              iov_off,
                              flags);
        iov_off += curr_read;
        tot_read += curr_read;
    }

    *bytes_read = tot_read;
    return ret;
}

int com_fs_vfs_writev(size_t      *bytes_written,
                      com_vnode_t *node,
                      kioviter_t  *ioviter,
                      uintmax_t    off,
                      uintmax_t    flags) {
    size_t dump = 0;
    if (NULL == bytes_written) {
        bytes_written = &dump;
    }

    if (NULL != node->ops->writev) {
        return node->ops->writev(bytes_written, node, ioviter, off, flags);
    }

    // If the node does not support iovec, try to stub it with sequential
    // accesses
    if (NULL == node->ops->write) {
        return ENOSYS;
    }

    struct iovec *iov;
    int           ret       = 0;
    size_t        tot_write = 0;

    for (size_t iov_off = off;
         NULL != (iov = kioviter_next(ioviter)) && 0 == ret;) {
        size_t curr_write = 0;
        ret               = node->ops->write(&curr_write,
                               node,
                               iov->iov_base,
                               iov->iov_len,
                               iov_off,
                               flags);
        iov_off += curr_write;
        tot_write += curr_write;
    }

    *bytes_written = tot_write;
    return ret;
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
int com_fs_vfs_mksocket(com_vnode_t **out,
                        com_vnode_t  *dir,
                        const char   *name,
                        size_t        namelen,
                        uintmax_t     attr) {

    if (NULL == dir->ops->mksocket) {
        return ENOSYS;
    }

    return dir->ops->mksocket(out, dir, name, namelen, attr, 0);
}

// UTILITY FUNCTIONS

int com_fs_vfs_alloc_vnode(com_vnode_t    **out,
                           com_vfs_t       *vfs,
                           com_vnode_type_t type,
                           com_vnode_ops_t *ops,
                           void            *extra) {
    com_vnode_t *new_node = NULL;
    int          ret      = 0;

    if (E_COM_VNODE_TYPE_SOCKET == type) {
        new_node = com_fs_sockfs_new();
    } else if (E_COM_VNODE_TYPE_FIFO == type) {
        ret = ENOSYS;
        goto end;
    } else {
        new_node = com_mm_slab_alloc(sizeof(com_vnode_t));
    }

    new_node->type    = type;
    new_node->num_ref = 1;
    new_node->vfs     = vfs;
    new_node->extra   = extra;
    new_node->ops     = ops;

end:
    *out = new_node;
    return ret;
}

int com_fs_vfs_create_any(com_vnode_t **out,
                          const char   *path,
                          size_t        pathlen,
                          com_vnode_t  *root,
                          com_vnode_t  *cwd,
                          uintmax_t     attr,
                          int (*create_handler)(com_vnode_t **out,
                                                com_vnode_t  *dir,
                                                const char   *name,
                                                size_t        namelen,
                                                uintmax_t     attr)) {
    com_vnode_t *vnode = NULL;
    com_vnode_t *dir   = NULL;
    int          ret   = 0;

    size_t penult_len, end_idx, end_len;
    kstrpathpenult(path, pathlen, &penult_len, &end_idx, &end_len);
    ret = com_fs_vfs_lookup(&dir, path, penult_len, root, cwd, true);
    if (0 != ret) {
        goto end;
    }

    KASSERT(NULL != dir);
    ret = create_handler(&vnode, dir, &path[end_idx], end_len, attr);

end:
    *out = vnode;
    COM_FS_VFS_VNODE_RELEASE(dir);
    return ret;
}
