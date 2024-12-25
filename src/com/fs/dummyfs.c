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

// NOTE: this is a dummy file system written to test the VFS
// it is not meant to be used or taken as an example
// it's here because I don't want to lete the code for now

#include <arch/info.h>
#include <kernel/com/fs/dummyfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <lib/mem.h>

struct dummyfs_node {
  char name[100];
  union {
    struct dummyfs_node *directory;
    char                 buf[100];
  };
  COM_FS_VFS_VNODE_t *vnode;
  bool         present;
};

static com_vfs_ops_t DummyfsOps =
    (com_vfs_ops_t){.mount = com_fs_dummyfs_mount, .vget = com_fs_dummyfs_vget};
static COM_FS_VFS_VNODE_ops_t DummyfsNodeOps =
    (COM_FS_VFS_VNODE_ops_t){.create = com_fs_dummyfs_create,
                      .write  = com_fs_dummyfs_write,
                      .read   = com_fs_dummyfs_read,
                      .lookup = com_fs_dummyfs_lookup};

static int kstrcmp(const char *str1, const char *str2, size_t max) {
  for (size_t i = 0; 0 != *str1 && 0 != *str2 && i < max; i++) {
    if (*str1 != *str2) {
      return 1;
    }

    str1++;
    str2++;
  }

  return 0;
}

// VFS OPS

int com_fs_dummyfs_vget(COM_FS_VFS_VNODE_t **out, com_vfs_t *vfs, void *inode) {
  DEBUG("running dummyfs vget for inode %x", inode);
  (void)vfs;
  struct dummyfs_node *dn = inode;
  *out                    = dn->vnode;
  return 0;
}

int com_fs_dummyfs_mount(com_vfs_t **out, COM_FS_VFS_VNODE_t *mountpoint) {
  DEBUG("mounting dummyfs in /");
  com_vfs_t *dummyfs        = com_mm_slab_alloc(sizeof(com_vfs_t));
  dummyfs->mountpoint       = mountpoint;
  dummyfs->ops              = &DummyfsOps;
  COM_FS_VFS_VNODE_t         *root = com_mm_slab_alloc(sizeof(COM_FS_VFS_VNODE_t));
  struct dummyfs_node *dfs_root =
      com_mm_slab_alloc(sizeof(struct dummyfs_node));
  root->type    = COM_FS_VFS_VNODE_TYPE_DIR;
  root->isroot  = true;
  root->ops     = &DummyfsNodeOps;
  root->extra   = dfs_root;
  root->num_ref = 1;
  root->vfs     = dummyfs;
  dummyfs->root = root;

  dfs_root->vnode   = root;
  dfs_root->present = true;
  dfs_root->name[0] = 0;
  void *rootbuf     = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  kmemset(rootbuf, ARCH_PAGE_SIZE, 0);
  dfs_root->directory = rootbuf;

  if (NULL != mountpoint) {
    mountpoint->mountpointof = dummyfs;
    mountpoint->type         = COM_FS_VFS_VNODE_TYPE_DIR;
  }

  *out = dummyfs;
  return 0;
}

// VNODE OPS

int com_fs_dummyfs_create(COM_FS_VFS_VNODE_t **out,
                          COM_FS_VFS_VNODE_t  *dir,
                          const char   *name,
                          size_t        namelen,
                          uintmax_t     attr) {
  (void)attr;
  COM_FS_VFS_VNODE_t *vn = com_mm_slab_alloc(sizeof(COM_FS_VFS_VNODE_t));

  vn->vfs     = dir->vfs;
  vn->ops     = dir->ops;
  vn->type    = COM_FS_VFS_VNODE_TYPE_FILE;
  vn->isroot  = false;
  vn->num_ref = 1;

  struct dummyfs_node *dirbuf = ((struct dummyfs_node *)dir->extra)->directory;
  struct dummyfs_node *first_free = NULL;

  for (size_t i = 0; i < ARCH_PAGE_SIZE / sizeof(struct dummyfs_node) - 1;
       i++) {
    if (!dirbuf[i].present) {
      DEBUG("dummyfs create: file created at index %u", i);
      first_free          = &dirbuf[i];
      first_free->present = true;
      break;
    }
  }

  ASSERT(NULL != first_free);

  first_free->present = true;
  kmemcpy(first_free->name, (void *)name, namelen);
  first_free->name[namelen] = 0;
  vn->extra                 = first_free;
  first_free->vnode         = vn;

  *out = vn;
  return 0;
}

int com_fs_dummyfs_write(size_t      *bytes_written,
                         COM_FS_VFS_VNODE_t *node,
                         void        *buf,
                         size_t       buflen,
                         uintmax_t    off,
                         uintmax_t    flags) {
  (void)flags;
  (void)bytes_written;
  struct dummyfs_node *dn      = node->extra;
  uint8_t             *bytebuf = buf;

  for (size_t i = off; i < 100 && i - off < buflen + off; i++) {
    dn->buf[i] = bytebuf[i - off];
  }

  return 0;
}

int com_fs_dummyfs_read(void        *buf,
                        size_t       buflen,
                        size_t      *bytes_read,
                        COM_FS_VFS_VNODE_t *node,
                        uintmax_t    off,
                        uintmax_t    flags) {
  (void)flags;
  (void)bytes_read;
  struct dummyfs_node *dn      = node->extra;
  uint8_t             *bytebuf = buf;

  for (size_t i = off; i < 100 && i - off < buflen; i++) {
    bytebuf[i - off] = dn->buf[i];
  }

  return 0;
}

int com_fs_dummyfs_lookup(COM_FS_VFS_VNODE_t **out,
                          COM_FS_VFS_VNODE_t  *dir,
                          const char   *name,
                          size_t        len) {
  struct dummyfs_node *inode  = ((struct dummyfs_node *)dir->extra);
  struct dummyfs_node *dirbuf = inode->directory;
  DEBUG("running dummyfs lookup on root=%u name=%s", dir->isroot, inode->name);
  struct dummyfs_node *found = NULL;
  size_t               max   = 100;
  if (len < max) {
    max = len;
  }

  for (size_t i = 0; i < ARCH_PAGE_SIZE / sizeof(struct dummyfs_node) - 1;
       i++) {
    DEBUG("dummyfs lookup: attempting index %u", i);
    if (dirbuf[i].present && 0 == kstrcmp(dirbuf[i].name, name, max)) {
      DEBUG("found: name=%s", dirbuf[i].name);
      found          = &dirbuf[i];
      found->present = true;
      break;
    }
  }

  ASSERT(NULL != found);

  *out = found->vnode;
  return 0;
}
