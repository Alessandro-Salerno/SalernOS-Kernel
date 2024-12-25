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

#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/tmpfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/log.h>
#include <kernel/com/mm/slab.h>

struct devfs_dev {
  COM_FS_VFS_VNODE_t   *vnode;
  com_dev_ops_t *devops;
  void          *devdata;
};

static COM_FS_VFS_VNODE_ops_t DevfsNodeOps =
    (COM_FS_VFS_VNODE_ops_t){.read   = com_fs_devfs_read,
                      .write  = com_fs_devfs_write,
                      .ioctl  = com_fs_devfs_ioctl,
                      .close  = com_fs_devfs_close,
                      .create = com_fs_devfs_create,
                      .mkdir  = com_fs_devfs_mkdir,
                      .lookup = com_fs_tmpfs_lookup};

static com_vfs_t *Devfs = NULL;

// VNODE OPS

// TODO: implement this
int com_fs_devfs_close(COM_FS_VFS_VNODE_t *vnode) {
  (void)vnode;
  return 0;
}

int com_fs_devfs_create(COM_FS_VFS_VNODE_t **out,
                        COM_FS_VFS_VNODE_t  *dir,
                        const char   *name,
                        size_t        namelen,
                        uintmax_t     attr) {
  COM_FS_VFS_VNODE_t *new = NULL;
  int ret          = com_fs_tmpfs_create(
      &new, dir, name, namelen, attr | COM_VFS_CREAT_ATTR_GHOST);

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

int com_fs_devfs_mkdir(COM_FS_VFS_VNODE_t **out,
                       COM_FS_VFS_VNODE_t  *parent,
                       const char   *name,
                       size_t        namelen,
                       uintmax_t     attr) {
  COM_FS_VFS_VNODE_t *new = NULL;
  int ret          = com_fs_tmpfs_mkdir(
      &new, parent, name, namelen, attr | COM_VFS_CREAT_ATTR_GHOST);

  if (0 != ret || NULL == new) {
    *out = NULL;
    return ret;
  }

  new->ops = &DevfsNodeOps;
  *out     = new;
  return 0;
}

int com_fs_devfs_read(void        *buf,
                      size_t       buflen,
                      size_t      *bytes_read,
                      COM_FS_VFS_VNODE_t *node,
                      uintmax_t    off,
                      uintmax_t    flags) {
  struct devfs_dev *dev = com_fs_tmpfs_get_other(node);
  return dev->devops->read(buf, buflen, bytes_read, dev->devdata, off, flags);
}

int com_fs_devfs_write(size_t      *bytes_written,
                       COM_FS_VFS_VNODE_t *node,
                       void        *buf,
                       size_t       buflen,
                       uintmax_t    off,
                       uintmax_t    flags) {
  struct devfs_dev *dev = com_fs_tmpfs_get_other(node);
  return dev->devops->write(
      bytes_written, dev->devdata, buf, buflen, off, flags);
}

int com_fs_devfs_ioctl(COM_FS_VFS_VNODE_t *node, uintmax_t op, void *buf) {
  struct devfs_dev *dev = com_fs_tmpfs_get_other(node);
  return dev->devops->ioctl(dev->devdata, op, buf);
}

// OTHER FUNCTIONS

int com_fs_devfs_register(COM_FS_VFS_VNODE_t  **out,
                          COM_FS_VFS_VNODE_t   *dir,
                          const char    *name,
                          size_t         namelen,
                          com_dev_ops_t *devops,
                          void          *devdata) {
  if (NULL == dir) {
    dir = Devfs->root;
  }

  COM_FS_VFS_VNODE_t *devnode = NULL;
  int          ret     = com_fs_devfs_create(&devnode, dir, name, namelen, 0);

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
  COM_FS_VFS_VNODE_t *devdir = NULL;
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
