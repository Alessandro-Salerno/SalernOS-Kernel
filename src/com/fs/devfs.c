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
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/tmpfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/slab.h>

struct devfs_dev {
    com_vnode_t   *vnode;
    com_dev_ops_t *devops;
    void          *devdata;
};

static com_vnode_ops_t DevfsNodeOps = (com_vnode_ops_t){
    .read      = com_fs_devfs_read,
    .write     = com_fs_devfs_write,
    .ioctl     = com_fs_devfs_ioctl,
    .close     = com_fs_devfs_close,
    .create    = com_fs_devfs_create,
    .mkdir     = com_fs_devfs_mkdir,
    .lookup    = com_fs_tmpfs_lookup,
    .isatty    = com_fs_devfs_isatty,
    .stat      = com_fs_devfs_stat,
    .poll_head = com_fs_devfs_poll_head,
    .poll      = com_fs_devfs_poll,
    .open      = com_fs_devfs_open};

static com_vfs_t *Devfs = NULL;

// VNODE OPS

int com_fs_devfs_close(com_vnode_t *node) {
    struct devfs_dev *dev = com_fs_tmpfs_get_other(node);

    if (NULL == dev->devops->close) {
        return ENOSYS;
    }

    return dev->devops->close(dev->devdata);
}

int com_fs_devfs_create(com_vnode_t **out,
                        com_vnode_t  *dir,
                        const char   *name,
                        size_t        namelen,
                        uintmax_t     attr,
                        uintmax_t     fsattr) {
    com_vnode_t *new = NULL;
    int          ret = com_fs_tmpfs_create(&new,
                                  dir,
                                  name,
                                  namelen,
                                  attr,
                                  fsattr | COM_FS_TMPFS_ATTR_GHOST);

    if (0 != ret || NULL == new) {
        *out = NULL;
        return ret;
    }

    struct devfs_dev *dev = com_mm_slab_alloc(sizeof(struct devfs_dev));
    com_fs_tmpfs_set_other(new, dev);
    new->ops = &DevfsNodeOps;
    *out     = new;
    return 0;
}

int com_fs_devfs_mkdir(com_vnode_t **out,
                       com_vnode_t  *parent,
                       const char   *name,
                       size_t        namelen,
                       uintmax_t     attr,
                       uintmax_t     fsattr) {
    if (NULL == parent) {
        parent = Devfs->root;
    }

    com_vnode_t *new = NULL;
    int          ret = com_fs_tmpfs_mkdir(&new,
                                 parent,
                                 name,
                                 namelen,
                                 attr,
                                 fsattr | COM_FS_TMPFS_ATTR_GHOST);

    if (0 != ret || NULL == new) {
        *out = NULL;
        return ret;
    }

    // new->ops = &DevfsNodeOps;
    *out = new;
    return 0;
}

int com_fs_devfs_read(void        *buf,
                      size_t       buflen,
                      size_t      *bytes_read,
                      com_vnode_t *node,
                      uintmax_t    off,
                      uintmax_t    flags) {
    struct devfs_dev *dev = com_fs_tmpfs_get_other(node);

    if (NULL == dev->devops->read) {
        return ENOSYS;
    }

    return dev->devops->read(buf, buflen, bytes_read, dev->devdata, off, flags);
}

int com_fs_devfs_write(size_t      *bytes_written,
                       com_vnode_t *node,
                       void        *buf,
                       size_t       buflen,
                       uintmax_t    off,
                       uintmax_t    flags) {
    struct devfs_dev *dev = com_fs_tmpfs_get_other(node);

    if (NULL == dev->devops->write) {
        return ENOSYS;
    }

    return dev->devops
        ->write(bytes_written, dev->devdata, buf, buflen, off, flags);
}

int com_fs_devfs_ioctl(com_vnode_t *node, uintmax_t op, void *buf) {
    struct devfs_dev *dev = com_fs_tmpfs_get_other(node);

    if (NULL == dev->devops->ioctl) {
        return ENOSYS;
    }

    return dev->devops->ioctl(dev->devdata, op, buf);
}

int com_fs_devfs_isatty(com_vnode_t *node) {
    struct devfs_dev *dev = com_fs_tmpfs_get_other(node);

    if (NULL == dev->devops->isatty) {
        return ENOSYS;
    }

    return dev->devops->isatty(dev->devdata);
}

int com_fs_devfs_stat(struct stat *out, com_vnode_t *node) {
    struct devfs_dev *dev = com_fs_tmpfs_get_other(node);

    if (NULL == dev->devops->stat) {
        return ENOSYS;
    }

    return dev->devops->stat(out, dev->devdata);
}

int com_fs_devfs_poll_head(struct com_poll_head **out, com_vnode_t *node) {
    struct devfs_dev *dev = com_fs_tmpfs_get_other(node);

    if (NULL == dev->devops->poll_head) {
        return ENOSYS;
    }

    return dev->devops->poll_head(out, dev->devdata);
}

int com_fs_devfs_poll(short *revents, com_vnode_t *node, short events) {
    struct devfs_dev *dev = com_fs_tmpfs_get_other(node);

    if (NULL == dev->devops->poll) {
        return ENOSYS;
    }

    return dev->devops->poll(revents, dev->devdata, events);
}

int com_fs_devfs_open(com_vnode_t **out, com_vnode_t *node) {
    struct devfs_dev *dev = com_fs_tmpfs_get_other(node);

    if (NULL == dev->devops->open) {
        *out = node;
        return 0;
    }

    return dev->devops->open(out, dev->devdata);
}

// OTHER FUNCTIONS

void *com_fs_devfs_get_data(com_vnode_t *node) {
    struct devfs_dev *dev = com_fs_tmpfs_get_other(node);
    return dev->devdata;
}

int com_fs_devfs_register(com_vnode_t  **out,
                          com_vnode_t   *dir,
                          const char    *name,
                          size_t         namelen,
                          com_dev_ops_t *devops,
                          void          *devdata) {
    if (NULL == dir) {
        dir = Devfs->root;
    }

    com_vnode_t *devnode = NULL;
    int          ret = com_fs_devfs_create(&devnode, dir, name, namelen, 0, 0);

    if (0 != ret || NULL == devnode) {
        *out = NULL;
        return ret;
    }

    struct devfs_dev *dev = com_fs_tmpfs_get_other(devnode);
    dev->devops           = devops;
    dev->devdata          = devdata;
    *out                  = devnode;
    return 0;
}

int com_fs_devfs_register_anonymous(com_vnode_t  **out,
                                    com_dev_ops_t *devops,
                                    void          *devdata) {
    com_vnode_t *devnode = NULL;
    int          ret     = com_fs_devfs_create(&devnode,
                                  Devfs->root,
                                  NULL,
                                  0,
                                  0,
                                  COM_FS_TMPFS_ATTR_NO_DIRENT);

    if (0 != ret || NULL == devnode) {
        *out = NULL;
        return ret;
    }

    struct devfs_dev *dev = com_fs_tmpfs_get_other(devnode);
    dev->devops           = devops;
    dev->devdata          = devdata;
    *out                  = devnode;
    return 0;
}

int com_fs_devfs_init(com_vfs_t **out, com_vfs_t *rootfs) {
    KLOG("mounting devfs in /dev/");
    com_vnode_t *devdir = NULL;
    int          ret    = com_fs_vfs_mkdir(&devdir, rootfs->root, "dev", 3, 0);

    if (0 != ret) {
        *out = NULL;
        return ret;
    }

    com_vfs_t *devfs = NULL;
    if (0 != com_fs_tmpfs_mount(&devfs, devdir)) {
        *out = NULL;
        return ret;
    }

    // devfs->root->ops = &DevfsNodeOps;
    Devfs = devfs;
    *out  = devfs;
    return 0;
}
