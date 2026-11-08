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

#include <errno.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/tmpfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/slab.h>

struct devfs_dev {
    com_dev_ops_t *devops;
    void          *devdata;
};

static com_vfs_t *Devfs = NULL;

static struct devfs_dev *dev_of(com_vnode_t *node) {
    return com_fs_tmpfs_private(node);
}

// VNODE OPS

static int devfs_open(com_vnode_t **out, com_vnode_t *node) {
    struct devfs_dev *dev = dev_of(node);
    if (NULL == dev->devops->open) {
        COM_FS_VFS_VNODE_HOLD(node);
        *out = node;
        return 0;
    }

    return dev->devops->open(out, dev->devdata);
}

static int devfs_close(com_vnode_t *node) {
    struct devfs_dev *dev = dev_of(node);
    if (NULL == dev->devops->close) {
        return 0;
    }

    return dev->devops->close(dev->devdata);
}

static void devfs_inactive(com_vnode_t *node) {
    struct devfs_dev *dev = dev_of(node);
    if (NULL != dev->devops->inactive) {
        dev->devops->inactive(dev->devdata);
    }

    com_mm_slab_free(dev, sizeof(struct devfs_dev));
    com_fs_tmpfs_inactive(node);
}

static int devfs_read(void        *buf,
                      size_t       buflen,
                      size_t      *bytes_read,
                      com_vnode_t *node,
                      uintmax_t    off,
                      uintmax_t    flags) {
    struct devfs_dev *dev = dev_of(node);
    if (NULL == dev->devops->read) {
        return ENOSYS;
    }

    return dev->devops->read(buf, buflen, bytes_read, dev->devdata, off, flags);
}

static int devfs_write(size_t      *bytes_written,
                       com_vnode_t *node,
                       void        *buf,
                       size_t       buflen,
                       uintmax_t    off,
                       uintmax_t    flags) {
    struct devfs_dev *dev = dev_of(node);
    if (NULL == dev->devops->write) {
        return ENOSYS;
    }

    return dev->devops
        ->write(bytes_written, dev->devdata, buf, buflen, off, flags);
}

static int devfs_ioctl(com_vnode_t *node, uintmax_t op, void *buf) {
    struct devfs_dev *dev = dev_of(node);
    if (NULL == dev->devops->ioctl) {
        return ENOSYS;
    }

    return dev->devops->ioctl(dev->devdata, op, buf);
}

static int devfs_isatty(com_vnode_t *node) {
    struct devfs_dev *dev = dev_of(node);
    if (NULL == dev->devops->isatty) {
        return ENOSYS;
    }

    return dev->devops->isatty(dev->devdata);
}

static int devfs_seekable(com_vnode_t *node) {
    struct devfs_dev *dev = dev_of(node);
    if (NULL == dev->devops->seekable) {
        return 0;
    }

    return dev->devops->seekable(dev->devdata);
}

static int devfs_stat(struct stat *out, com_vnode_t *node) {
    struct devfs_dev *dev = dev_of(node);
    int ret = NULL != dev->devops->stat ? dev->devops->stat(out, dev->devdata)
                                        : ENOSYS;
    if (0 == ret || ENOSYS == ret) {
        out->st_nlink = com_fs_tmpfs_nlink(node);
    }
    return ret;
}

static int devfs_poll_head(struct com_poll_head **out, com_vnode_t *node) {
    struct devfs_dev *dev = dev_of(node);
    if (NULL == dev->devops->poll_head) {
        return ENOSYS;
    }

    return dev->devops->poll_head(out, dev->devdata);
}

static int devfs_poll(short *revents, com_vnode_t *node, short events) {
    struct devfs_dev *dev = dev_of(node);
    if (NULL == dev->devops->poll) {
        return ENOSYS;
    }

    return dev->devops->poll(revents, dev->devdata, events);
}

static int devfs_mmap(void             **out,
                      com_vnode_t       *node,
                      com_vmm_context_t *vmm_context,
                      void              *hint,
                      size_t             size,
                      int                vmm_flags,
                      arch_mmu_flags_t   mmu_flags,
                      uintmax_t          off) {
    struct devfs_dev *dev = dev_of(node);
    if (NULL == dev->devops->mmap) {
        return ENOSYS;
    }

    return dev->devops->mmap(out,
                             dev->devdata,
                             vmm_context,
                             hint,
                             size,
                             vmm_flags,
                             mmu_flags,
                             off);
}

static com_vnode_ops_t DevfsNodeOps = {.open      = devfs_open,
                                       .close     = devfs_close,
                                       .inactive  = devfs_inactive,
                                       .read      = devfs_read,
                                       .write     = devfs_write,
                                       .ioctl     = devfs_ioctl,
                                       .isatty    = devfs_isatty,
                                       .seekable  = devfs_seekable,
                                       .stat      = devfs_stat,
                                       .poll_head = devfs_poll_head,
                                       .poll      = devfs_poll,
                                       .mmap      = devfs_mmap};

// INTERFACE

static int make_dev_attr(com_vnode_attr_t       *attr,
                         com_fs_tmpfs_special_t *special,
                         struct devfs_dev      **dev,
                         com_dev_ops_t          *devops,
                         void                   *devdata) {
    KASSERT(E_COM_VNODE_TYPE_CHARDEV == devops->type ||
            E_COM_VNODE_TYPE_BLOCKDEV == devops->type);

    *dev = com_mm_slab_alloc(sizeof(struct devfs_dev));
    if (NULL == *dev) {
        return ENOMEM;
    }

    (*dev)->devops   = devops;
    (*dev)->devdata  = devdata;
    special->ops     = &DevfsNodeOps;
    special->private = *dev;
    *attr            = COM_FS_VFS_ATTR(devops->type);
    attr->fsdata     = special;
    return 0;
}

int com_fs_devfs_register(com_vnode_t  **out,
                          com_vnode_t   *dir,
                          const char    *name,
                          size_t         namelen,
                          com_dev_ops_t *devops,
                          void          *devdata) {
    com_vnode_attr_t       attr;
    com_fs_tmpfs_special_t special;
    struct devfs_dev      *dev;
    int ret = make_dev_attr(&attr, &special, &dev, devops, devdata);
    if (0 != ret) {
        return ret;
    }

    com_vnode_t *node = NULL;
    ret               = com_fs_vfs_create(&node,
                                          NULL != dir ? dir : Devfs->root,
                                          name,
                                          namelen,
                                          &attr);
    if (0 != ret) {
        com_mm_slab_free(dev, sizeof(struct devfs_dev));
        return ret;
    }

    if (NULL != out) {
        *out = node;
    } else {
        COM_FS_VFS_VNODE_RELEASE(node);
    }

    return 0;
}

int com_fs_devfs_register_anonymous(com_vnode_t  **out,
                                    com_dev_ops_t *devops,
                                    void          *devdata) {
    com_vnode_attr_t       attr;
    com_fs_tmpfs_special_t special;
    struct devfs_dev      *dev;
    int ret = make_dev_attr(&attr, &special, &dev, devops, devdata);
    if (0 != ret) {
        return ret;
    }

    ret = com_fs_tmpfs_new_orphan(out, Devfs, &attr);
    if (0 != ret) {
        com_mm_slab_free(dev, sizeof(struct devfs_dev));
    }

    return ret;
}

int com_fs_devfs_mkdir(com_vnode_t **out,
                       com_vnode_t  *parent,
                       const char   *name,
                       size_t        namelen) {
    return com_fs_vfs_create(out,
                             NULL != parent ? parent : Devfs->root,
                             name,
                             namelen,
                             &COM_FS_VFS_ATTR(E_COM_VNODE_TYPE_DIR));
}

void *com_fs_devfs_get_data(com_vnode_t *node) {
    return dev_of(node)->devdata;
}

int com_fs_devfs_init(com_vfs_t **out, com_vfs_t *rootfs) {
    KLOG("mounting devfs in /dev/");
    *out             = NULL;
    com_vnode_t *dir = NULL;
    int ret = com_fs_vfs_create(&dir,
                                rootfs->root,
                                "dev",
                                3,
                                &COM_FS_VFS_ATTR(E_COM_VNODE_TYPE_DIR));

    // The initrd may already have /dev
    if (EEXIST == ret) {
        ret = com_fs_vfs_lookup(&dir, "dev", 3, rootfs->root, rootfs->root, 0);
    }
    if (0 != ret) {
        return ret;
    }

    com_vfs_t *devfs = NULL;
    ret              = com_fs_tmpfs_mount(&devfs, dir);
    COM_FS_VFS_VNODE_RELEASE(dir);
    if (0 != ret) {
        return ret;
    }

    // Never dropped, so devfs is never unmounted
    COM_FS_VFS_VNODE_HOLD(devfs->root);
    Devfs = devfs;
    *out  = devfs;
    return 0;
}
