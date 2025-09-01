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

#include <arch/info.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/pagecache.h>
#include <kernel/com/fs/tmpfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/slab.h>
#include <lib/mem.h>
#include <lib/spinlock.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <vendor/tailq.h>

struct tmpfs_dir_entry {
    TAILQ_ENTRY(tmpfs_dir_entry) entries;
    struct tmpfs_node *tnode;
    size_t             namelen;
    char               name[];
};

TAILQ_HEAD(tmpfs_dir_entries, tmpfs_dir_entry);

struct tmpfs_node {
    com_vnode_t            *vnode;
    size_t                  num_links;
    struct tmpfs_dir_entry *dirent;
    struct tmpfs_node      *parent;

    // TODO: turn this into a mutex
    kspinlock_t lock;

    union {
        struct {
            struct tmpfs_dir_entries entries;
        } dir;
        struct {
            size_t           size;
            com_pagecache_t *data;
        } file;
        struct {
            const char *path;
            size_t      len;
        } link;
        struct {
            void *data;
        } other;
    };
};

static com_vfs_ops_t   TmpfsOps     = {.mount = com_fs_tmpfs_mount};
static com_vnode_ops_t TmpfsNodeOps = {.create   = com_fs_tmpfs_create,
                                       .mkdir    = com_fs_tmpfs_mkdir,
                                       .lookup   = com_fs_tmpfs_lookup,
                                       .read     = com_fs_tmpfs_read,
                                       .write    = com_fs_tmpfs_write,
                                       .symlink  = com_fs_tmpfs_symlink,
                                       .readlink = com_fs_tmpfs_readlink,
                                       .unlink   = com_fs_tmpfs_unlink,
                                       .stat     = com_fs_tmpfs_stat,
                                       .truncate = com_fs_tmpfs_truncate,
                                       .readdir  = com_fs_tmpfs_readdir,
                                       .vnctl    = com_fs_tmpfs_vnctl,
                                       .mksocket = com_fs_tmpfs_mksocket};

// SUPPORT FUNCTIONS

static int create_common(struct tmpfs_dir_entry **outent,
                         com_vnode_t            **out,
                         com_vnode_t             *dir,
                         const char              *name,
                         size_t                   namelen,
                         uintmax_t                attr,
                         uintmax_t                fsattr,
                         com_vnode_type_t         type) {
    (void)attr;
    KASSERT(E_COM_VNODE_TYPE_DIR == dir->type);

    struct tmpfs_node *tn_new = com_mm_slab_alloc(sizeof(struct tmpfs_node));
    tn_new->lock              = KSPINLOCK_NEW();
    tn_new->num_links         = 1;

    struct tmpfs_dir_entry *dirent = NULL;
    if (!(COM_FS_TMPFS_ATTR_NO_DIRENT & fsattr)) {
        dirent = com_mm_slab_alloc(sizeof(struct tmpfs_dir_entry) + namelen);
        dirent->tnode   = tn_new;
        dirent->namelen = namelen;
        kmemcpy(dirent->name, name, namelen);
    }

    com_fs_vfs_alloc_vnode(out, dir->vfs, type, &TmpfsNodeOps, tn_new);
    tn_new->vnode = *out;

    if (NULL != dirent) {
        tn_new->dirent = dirent;
    }

    *outent = dirent;
    return 0;
}

// VFS OPS

int com_fs_tmpfs_vget(com_vnode_t **out, com_vfs_t *vfs, void *inode) {
    (void)vfs;
    struct tmpfs_node *tnode = inode;

    if (NULL != tnode->vnode) {
        COM_FS_VFS_VNODE_HOLD(tnode->vnode);
        *out = tnode->vnode;
        return 0;
    }

    return ENOENT;
}

int com_fs_tmpfs_mount(com_vfs_t **out, com_vnode_t *mountpoint) {
    com_vfs_t         *tmpfs   = com_mm_slab_alloc(sizeof(com_vfs_t));
    struct tmpfs_node *tn_root = com_mm_slab_alloc(sizeof(struct tmpfs_node));
    com_vnode_t       *vn_root = NULL;

    tn_root->lock = KSPINLOCK_NEW();
    TAILQ_INIT(&tn_root->dir.entries);
    com_fs_vfs_alloc_vnode(&vn_root,
                           tmpfs,
                           E_COM_VNODE_TYPE_DIR,
                           &TmpfsNodeOps,
                           tn_root);
    tn_root->dirent = NULL;
    tn_root->parent = NULL;
    vn_root->isroot = true;
    tn_root->vnode  = vn_root;

    tmpfs->root       = vn_root;
    tmpfs->mountpoint = mountpoint;
    tmpfs->ops        = &TmpfsOps;

    if (NULL != mountpoint) {
        KASSERT(E_COM_VNODE_TYPE_DIR == mountpoint->type);
        mountpoint->mountpointof = tmpfs;
    }

    *out = tmpfs;
    return 0;
}

// NODE OPS

int com_fs_tmpfs_create(com_vnode_t **out,
                        com_vnode_t  *dir,
                        const char   *name,
                        size_t        namelen,
                        uintmax_t     attr,
                        uintmax_t     fsattr) {
    struct tmpfs_dir_entry *dirent = NULL;
    int                     inner  = create_common(&dirent,
                              out,
                              dir,
                              name,
                              namelen,
                              attr,
                              fsattr,
                              E_COM_VNODE_TYPE_FILE);
    if (0 != inner) {
        return inner;
    }

    struct tmpfs_node *tn = (*out)->extra;

    if (!(COM_FS_TMPFS_ATTR_GHOST & fsattr)) {
        tn->file.size = 0;
        tn->file.data = com_fs_pagecache_new();
    }

    if (NULL != dirent) {
        struct tmpfs_node *parent = dir->extra;
        kspinlock_acquire(&parent->lock);
        TAILQ_INSERT_TAIL(&parent->dir.entries, dirent, entries);
        tn->parent = parent;
        kspinlock_release(&parent->lock);
    }

    return 0;
}

int com_fs_tmpfs_mkdir(com_vnode_t **out,
                       com_vnode_t  *parent,
                       const char   *name,
                       size_t        namelen,
                       uintmax_t     attr,
                       uintmax_t     fsattr) {
    struct tmpfs_dir_entry *dirent = NULL;
    int                     inner  = create_common(&dirent,
                              out,
                              parent,
                              name,
                              namelen,
                              attr,
                              fsattr,
                              E_COM_VNODE_TYPE_DIR);
    if (0 != inner) {
        return inner;
    }

    struct tmpfs_node *tn          = (*out)->extra;
    struct tmpfs_node *parent_data = parent->extra;

    tn->parent = parent_data;
    TAILQ_INIT(&tn->dir.entries);

    kspinlock_acquire(&parent_data->lock);
    TAILQ_INSERT_TAIL(&parent_data->dir.entries, dirent, entries);
    kspinlock_release(&parent_data->lock);

    return 0;
}

int com_fs_tmpfs_lookup(com_vnode_t **out,
                        com_vnode_t  *dir,
                        const char   *name,
                        size_t        len) {
    KASSERT(E_COM_VNODE_TYPE_DIR == dir->type);
    struct tmpfs_node *dir_data = dir->extra;
    int                ret      = 0;

    if (2 == len && 0 == kmemcmp(name, "..", 2)) {
        if (NULL != dir_data->parent) {
            *out = dir_data->parent->vnode;
            COM_FS_VFS_VNODE_HOLD((*out));
            return 0;
        }

        *out = dir;
        return 0;
    }

    kspinlock_acquire(&dir_data->lock);

    struct tmpfs_dir_entry *entry, *_;
    TAILQ_FOREACH_SAFE(entry, &dir_data->dir.entries, entries, _) {
        if (len == entry->namelen && 0 == kmemcmp(entry->name, name, len)) {
            *out = entry->tnode->vnode;
            COM_FS_VFS_VNODE_HOLD((*out));
            goto cleanup;
        }
    }

    *out = NULL;
    ret  = ENOENT;

cleanup:
    kspinlock_release(&dir_data->lock);
    return ret;
}

// TODO: read locks?
int com_fs_tmpfs_read(void        *buf,
                      size_t       buflen,
                      size_t      *bytes_read,
                      com_vnode_t *node,
                      uintmax_t    off,
                      uintmax_t    flags) {
    (void)flags;
    if (E_COM_VNODE_TYPE_DIR == node->type) {
        return EISDIR;
    }

    struct tmpfs_node *file = node->extra;

    if (off >= file->file.size) {
        *bytes_read = 0;
        return 0;
    }

    if (off + buflen >= file->file.size) {
        buflen = 0;
        if (file->file.size > off) {
            buflen = file->file.size - off;
        }
    }

    if (0 == buflen) {
        *bytes_read = 0;
        return 0;
    }

    size_t read_count = 0;
    kspinlock_acquire(&file->lock);

    for (uintmax_t cur = off; cur < off + buflen;) {
        uintptr_t page;
        bool      page_present = com_fs_pagecache_get(&page,
                                                 file->file.data,
                                                 cur / ARCH_PAGE_SIZE);

        // ~((uintptr_t)ARCH_PAGE_SIZE - 1) is like & 0b111...000 so it is a
        // mask to floor the value to a multiple of ARCH_PAGE_SIZE. Then
        // ARCH_PAGE_SIZE is added to get the top address
        uintptr_t page_base = cur & ~((uintptr_t)ARCH_PAGE_SIZE - 1);
        uintptr_t end       = page_base + ARCH_PAGE_SIZE;

        if (end > off + buflen) {
            end = off + buflen;
        }

        if (page_present) {
            kmemcpy((uint8_t *)buf + cur - off,
                    (uint8_t *)page + (cur % ARCH_PAGE_SIZE),
                    end - cur);
        } else {
            kmemset((uint8_t *)buf + cur - off, end - cur, 0);
        }

        read_count += end - cur;
        cur = end;
    }

    kspinlock_release(&file->lock);
    *bytes_read = read_count;
    return 0;
}

// TODO: lock only the write side?
int com_fs_tmpfs_write(size_t      *bytes_written,
                       com_vnode_t *node,
                       void        *buf,
                       size_t       buflen,
                       uintmax_t    off,
                       uintmax_t    flags) {
    (void)flags;
    if (E_COM_VNODE_TYPE_DIR == node->type) {
        return EISDIR;
    }

    if (0 == buflen) {
        return 0;
    }

    struct tmpfs_node *file        = node->extra;
    size_t             write_count = 0;
    kspinlock_acquire(&file->lock);

    if (off + buflen > file->file.size) {
        file->file.size = off + buflen;
    }

    for (uintmax_t cur = off; cur < off + buflen;) {
        uintptr_t page;
        com_fs_pagecache_default(&page, file->file.data, cur / ARCH_PAGE_SIZE);

        // ~((uintptr_t)ARCH_PAGE_SIZE - 1) is like & 0b111...000 so it is a
        // mask to floor the value to a multiple of ARCH_PAGE_SIZE. Then
        // ARCH_PAGE_SIZE is added to get the top address
        uintptr_t page_base = cur & ~((uintptr_t)ARCH_PAGE_SIZE - 1);
        uintptr_t end       = page_base + ARCH_PAGE_SIZE;

        if (end > off + buflen) {
            end = off + buflen;
        }

        kmemcpy((uint8_t *)page + (cur % ARCH_PAGE_SIZE),
                (uint8_t *)buf + cur - off,
                end - cur);
        write_count += end - cur;
        cur = end;
    }

    kspinlock_release(&file->lock);
    *bytes_written = write_count;
    return 0;
}

int com_fs_tmpfs_symlink(com_vnode_t *dir,
                         const char  *linkname,
                         size_t       linknamelen,
                         const char  *path,
                         size_t       pathlen) {
    struct tmpfs_dir_entry *dirent;
    com_vnode_t            *vn;
    create_common(&dirent,
                  &vn,
                  dir,
                  linkname,
                  linknamelen,
                  0,
                  0,
                  E_COM_VNODE_TYPE_LINK);
    struct tmpfs_node *tn = vn->extra;
    vn->type              = E_COM_VNODE_TYPE_LINK;

    char *path_copy = com_mm_slab_alloc(pathlen);
    kmemcpy(path_copy, path, pathlen);
    tn->link.path = path_copy;
    tn->link.len  = pathlen;

    struct tmpfs_node *parent = dir->extra;
    kspinlock_acquire(&parent->lock);
    TAILQ_INSERT_TAIL(&parent->dir.entries, dirent, entries);
    tn->parent = parent;
    kspinlock_release(&parent->lock);

    return 0;
}

int com_fs_tmpfs_readlink(const char **path,
                          size_t      *pathlen,
                          com_vnode_t *link) {
    struct tmpfs_node *tn = link->extra;
    KASSERT(E_COM_VNODE_TYPE_LINK == link->type);
    *path    = tn->link.path;
    *pathlen = tn->link.len;
    KDEBUG("readlink: %.*s", *pathlen, *path);
    return 0;
}

int com_fs_tmpfs_unlink(com_vnode_t *node, int flags) {
    struct tmpfs_node *to_unlink_tn = node->extra;
    struct tmpfs_node *parent_tn    = to_unlink_tn->parent;
    int                ret          = 0;

    if (E_COM_VNODE_TYPE_DIR == node->type) {
        if (!(AT_REMOVEDIR & flags)) {
            ret = EISDIR;
            goto end;
        }
        kspinlock_acquire(&to_unlink_tn->lock);
        if (!TAILQ_EMPTY(&to_unlink_tn->dir.entries)) {
            kspinlock_release(&to_unlink_tn->lock);
            ret = ENOTEMPTY;
            goto end;
        }
        kspinlock_release(&to_unlink_tn->lock);
    }

    if (NULL != parent_tn) {
        kspinlock_acquire(&parent_tn->lock);
        TAILQ_REMOVE(&parent_tn->dir.entries, to_unlink_tn->dirent, entries);
        kspinlock_release(&parent_tn->lock);
        COM_FS_VFS_VNODE_RELEASE(node);
    }

end:
    return ret;
}

int com_fs_tmpfs_stat(struct stat *out, com_vnode_t *node) {
    struct tmpfs_node *file = node->extra;
    out->st_blksize         = 512;
    out->st_ino             = (unsigned long)file;
    out->st_mode = 01777; // TODO: this may cause issues (ke says it makes xorg
                          // "happy")

    if (E_COM_VNODE_TYPE_FILE == node->type) {
        out->st_mode |= S_IFREG;
        out->st_size = file->file.size;
    } else if (E_COM_VNODE_TYPE_DIR == node->type) {
        out->st_mode |= S_IFDIR;
    } else if (E_COM_VNODE_TYPE_LINK == node->type) {
        out->st_mode |= S_IFLNK;
    } else {
        return ENOSYS;
    }

    return 0;
}

int com_fs_tmpfs_truncate(com_vnode_t *node, size_t size) {
    if (E_COM_VNODE_TYPE_DIR == node->type) {
        return EISDIR;
    }

    struct tmpfs_node *file = node->extra;
    kspinlock_acquire(&file->lock);
    file->file.size = size;
    kspinlock_release(&file->lock);
    return 0;
}

int com_fs_tmpfs_readdir(void        *buf,
                         size_t       buflen,
                         size_t      *bytes_read,
                         com_vnode_t *dir,
                         uintmax_t    off) {
    if (E_COM_VNODE_TYPE_DIR != dir->type) {
        return ENOTDIR;
    }

    if (0 == buflen) {
        return 0;
    }

    struct tmpfs_node *node   = dir->extra;
    com_dirent_t      *dirent = buf;

    // return "." entry
    if (0 == off) {
        if (buflen < sizeof(com_dirent_t) + sizeof(".")) {
            return EOVERFLOW; // is this right?
        }

        dirent->reclen = sizeof(com_dirent_t) + sizeof(".");
        dirent->ino    = (ino_t)node;
        dirent->off    = 0;
        dirent->type   = DT_DIR;
        kmemcpy(dirent->name, ".", sizeof("."));
        *bytes_read = dirent->reclen;
        return 0;
    }

    // return ".." entry
    if (1 == off) {
        if (buflen < sizeof(com_dirent_t) + sizeof("..")) {
            return EOVERFLOW;
        }

        dirent->reclen = sizeof(com_dirent_t) + sizeof("..");
        dirent->ino    = (ino_t)node->parent;
        dirent->off    = 1;
        dirent->type   = DT_DIR;
        kmemcpy(dirent->name, "..", sizeof(".."));
        *bytes_read = dirent->reclen;
        return 0;
    }

    int ret = 0;

    kspinlock_acquire(&node->lock);

    struct tmpfs_dir_entry *cur = TAILQ_FIRST(&node->dir.entries);
    for (uintmax_t i = 0; i < off - 2 && NULL != cur; i++) {
        cur = TAILQ_NEXT(cur, entries);
    }

    // it could be done inside the loop, but there may be issues with the first
    // one being null
    if (NULL == cur) {
        *bytes_read = 0;
        ret         = 0;
        goto end;
    }

    size_t req_size = sizeof(com_dirent_t) + cur->namelen + 1;
    if (buflen < req_size) {
        ret = EOVERFLOW;
        goto end;
    }

    dirent->reclen = req_size;
    dirent->ino    = (ino_t)cur->tnode;
    kmemcpy(dirent->name, cur->name, cur->namelen);
    dirent->name[cur->namelen] = 0;
    dirent->off                = off;
    *bytes_read                = req_size;

    if (NULL == cur->tnode->vnode) {
        dirent->type = DT_UNKNOWN;
    } else if (E_COM_VNODE_TYPE_DIR == cur->tnode->vnode->type) {
        dirent->type = DT_DIR;
    } else if (E_COM_VNODE_TYPE_FILE == cur->tnode->vnode->type) {
        dirent->type = DT_REG;
    } else if (E_COM_VNODE_TYPE_LINK == cur->tnode->vnode->type) {
        dirent->type = DT_LNK;
    } else if (E_COM_VNODE_TYPE_CHARDEV == cur->tnode->vnode->type) {
        dirent->type = DT_CHR;
    } else if (E_COM_VNODE_TYPE_FIFO == cur->tnode->vnode->type) {
        dirent->type = DT_FIFO;
    } else if (E_COM_VNODE_TYPE_SOCKET == cur->tnode->vnode->type) {
        dirent->type = DT_SOCK;
    } else if (E_COM_VNODE_TYPE_BLOCKDEV == cur->tnode->vnode->type) {
        dirent->type = DT_BLK;
    } else {
        dirent->type = DT_UNKNOWN;
    }

end:
    kspinlock_release(&node->lock);
    return ret;
}

int com_fs_tmpfs_vnctl(com_vnode_t *node, uintmax_t op, void *buf) {
    int ret = ENOTSUP;

    struct tmpfs_node *tnode = node->extra;
    kspinlock_acquire(&tnode->lock);

    if (COM_FS_VFS_VNCTL_GETNAME == op) {
        struct tmpfs_dir_entry *dirent  = tnode->dirent;
        com_vnctl_name_t       *namebuf = buf;
        if (NULL != dirent) {
            namebuf->name    = dirent->name;
            namebuf->namelen = dirent->namelen;
        } else {
            namebuf->name    = "";
            namebuf->namelen = 0;
        }
        ret = 0;
    }

    kspinlock_release(&tnode->lock);
    return ret;
}

int com_fs_tmpfs_mksocket(com_vnode_t **out,
                          com_vnode_t  *dir,
                          const char   *name,
                          size_t        namelen,
                          uintmax_t     attr,
                          uintmax_t     fsattr) {
    struct tmpfs_dir_entry *dirent = NULL;
    int                     inner  = create_common(&dirent,
                              out,
                              dir,
                              name,
                              namelen,
                              attr,
                              fsattr,
                              E_COM_VNODE_TYPE_SOCKET);
    if (0 != inner) {
        return inner;
    }

    struct tmpfs_node *tn = (*out)->extra;
    (*out)->type          = E_COM_VNODE_TYPE_SOCKET;

    if (NULL != dirent) {
        struct tmpfs_node *parent = dir->extra;
        kspinlock_acquire(&parent->lock);
        TAILQ_INSERT_TAIL(&parent->dir.entries, dirent, entries);
        tn->parent = parent;
        kspinlock_release(&parent->lock);
    }

    return 0;
}

// OTHER FUNCTIONS

void com_fs_tmpfs_set_other(com_vnode_t *vnode, void *data) {
    struct tmpfs_node *n = vnode->extra;
    n->other.data        = data;
}

void *com_fs_tmpfs_get_other(com_vnode_t *vnode) {
    struct tmpfs_node *n = vnode->extra;
    return n->other.data;
}
