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
#include <errno.h>
#include <kernel/com/fs/pagecache.h>
#include <kernel/com/fs/tmpfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/spinlock.h>
#include <lib/mem.h>
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
    com_vnode_t *vnode;
    size_t       num_links;

    // TODO: turn this into a mutex
    com_spinlock_t lock;

    union {
        struct {
            struct tmpfs_dir_entries entries;
            struct tmpfs_node       *parent;
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
                                       .isatty   = com_fs_tmpfs_isatty,
                                       .stat     = com_fs_tmpfs_stat,
                                       .truncate = com_fs_tmpfs_truncate};

// SUPPORT FUNCTIONS
static int createat(struct tmpfs_dir_entry **outent,
                    com_vnode_t            **out,
                    com_vnode_t             *dir,
                    const char              *name,
                    size_t                   namelen,
                    uintmax_t                attr) {
    (void)attr;
    KASSERT(COM_VNODE_TYPE_DIR == dir->type);

    struct tmpfs_node *tn_new = com_mm_slab_alloc(sizeof(struct tmpfs_node));
    tn_new->lock              = COM_SPINLOCK_NEW();
    tn_new->num_links         = 1;

    struct tmpfs_dir_entry *dirent =
        com_mm_slab_alloc(sizeof(struct tmpfs_dir_entry) + namelen);
    dirent->tnode   = tn_new;
    dirent->namelen = namelen;
    kmemcpy(dirent->name, name, namelen);

    com_fs_tmpfs_vget(out, dir->vfs, tn_new);

    *outent = dirent;
    return 0;
}

// VFS OPS

int com_fs_tmpfs_vget(com_vnode_t **out, com_vfs_t *vfs, void *inode) {
    struct tmpfs_node *tnode = inode;

    if (NULL != tnode->vnode) {
        COM_FS_VFS_VNODE_HOLD(tnode->vnode);
        *out = tnode->vnode;
        return 0;
    }

    com_vnode_t *vnode  = com_mm_slab_alloc(sizeof(com_vnode_t));
    vnode->mountpointof = NULL;
    vnode->num_ref      = 1;
    vnode->vfs          = vfs;
    vnode->ops          = &TmpfsNodeOps;
    vnode->extra        = inode;

    tnode->vnode = vnode;
    *out         = vnode;
    return 0;
}

int com_fs_tmpfs_mount(com_vfs_t **out, com_vnode_t *mountpoint) {
    com_vfs_t         *tmpfs   = com_mm_slab_alloc(sizeof(com_vfs_t));
    struct tmpfs_node *tn_root = com_mm_slab_alloc(sizeof(struct tmpfs_node));
    com_vnode_t       *vn_root = NULL;

    tn_root->lock = COM_SPINLOCK_NEW();
    TAILQ_INIT(&tn_root->dir.entries);
    com_fs_tmpfs_vget(&vn_root, tmpfs, tn_root);
    vn_root->isroot = true;
    vn_root->type   = COM_VNODE_TYPE_DIR;

    tmpfs->root       = vn_root;
    tmpfs->mountpoint = mountpoint;
    tmpfs->ops        = &TmpfsOps;

    if (NULL != mountpoint) {
        KASSERT(COM_VNODE_TYPE_DIR == mountpoint->type);
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
                        uintmax_t     attr) {
    struct tmpfs_dir_entry *dirent = NULL;
    int inner = createat(&dirent, out, dir, name, namelen, attr);

    if (0 != inner) {
        return inner;
    }

    if (!(COM_VFS_CREAT_ATTR_GHOST & attr)) {
        struct tmpfs_node *tn = (*out)->extra;
        tn->file.size         = 0;
        tn->file.data         = com_fs_pagecache_new();
        (*out)->type          = COM_VNODE_TYPE_FILE;
    }

    struct tmpfs_node *parent = dir->extra;
    com_spinlock_acquire(&parent->lock);
    TAILQ_INSERT_TAIL(&parent->dir.entries, dirent, entries);
    com_spinlock_release(&parent->lock);

    return 0;
}

int com_fs_tmpfs_mkdir(com_vnode_t **out,
                       com_vnode_t  *parent,
                       const char   *name,
                       size_t        namelen,
                       uintmax_t     attr) {
    struct tmpfs_dir_entry *dirent = NULL;
    int inner = createat(&dirent, out, parent, name, namelen, attr);

    if (0 != inner) {
        return inner;
    }

    struct tmpfs_node *tn          = (*out)->extra;
    struct tmpfs_node *parent_data = parent->extra;

    tn->dir.parent = parent_data;
    TAILQ_INIT(&tn->dir.entries);
    (*out)->type = COM_VNODE_TYPE_DIR;

    com_spinlock_acquire(&parent_data->lock);
    TAILQ_INSERT_TAIL(&parent_data->dir.entries, dirent, entries);
    com_spinlock_release(&parent_data->lock);

    return 0;
}

int com_fs_tmpfs_lookup(com_vnode_t **out,
                        com_vnode_t  *dir,
                        const char   *name,
                        size_t        len) {
    KASSERT(COM_VNODE_TYPE_DIR == dir->type);
    struct tmpfs_node *dir_data = dir->extra;
    int                ret      = 0;

    if (2 == len && 0 == kmemcmp(name, "..", 2)) {
        if (NULL != dir_data->dir.parent) {
            *out = dir_data->dir.parent->vnode;
            COM_FS_VFS_VNODE_HOLD((*out));
            return 0;
        }

        *out = dir;
        return 0;
    }

    com_spinlock_acquire(&dir_data->lock);

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
    com_spinlock_release(&dir_data->lock);
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
    if (COM_VNODE_TYPE_DIR == node->type) {
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
    com_spinlock_acquire(&file->lock);

    for (uintmax_t cur = off; cur < off + buflen;) {
        uintptr_t page;
        bool      page_present =
            com_fs_pagecache_get(&page, file->file.data, cur / ARCH_PAGE_SIZE);

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

    com_spinlock_release(&file->lock);
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
    if (COM_VNODE_TYPE_DIR == node->type) {
        return EISDIR;
    }

    if (0 == buflen) {
        return 0;
    }

    struct tmpfs_node *file        = node->extra;
    size_t             write_count = 0;
    com_spinlock_acquire(&file->lock);

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

        // TODO: this is probably correct, but if something goes wrong, check
        // the mod
        kmemcpy((uint8_t *)page + (cur % ARCH_PAGE_SIZE),
                (uint8_t *)buf + cur - off,
                end - cur);
        write_count += end - cur;
        cur = end;
    }

    com_spinlock_release(&file->lock);
    *bytes_written = write_count;
    return 0;
}

int com_fs_tmpfs_isatty(com_vnode_t *node) {
    (void)node;
    return ENOTTY;
}

int com_fs_tmpfs_stat(struct stat *out, com_vnode_t *node) {
    struct tmpfs_node *file = node->extra;
    out->st_blksize         = 512;
    out->st_ino             = (unsigned long)file;
    out->st_mode =
        01777; // TODO: this may cause issues (ke says it makes xorg "happy")

    if (COM_VNODE_TYPE_FILE == node->type) {
        out->st_mode |= S_IFREG;
        out->st_size = file->file.size;
    } else if (COM_VNODE_TYPE_DIR == node->type) {
        out->st_mode |= S_IFDIR;
    } else {
        return ENOSYS;
    }

    // TODO: links
    return 0;
}

int com_fs_tmpfs_truncate(com_vnode_t *node, size_t size) {
    if (COM_VNODE_TYPE_DIR == node->type) {
        return EISDIR;
    }

    struct tmpfs_node *file = node->extra;
    com_spinlock_acquire(&file->lock);
    file->file.size = size;
    // TODO: evict from page cache
    com_spinlock_release(&file->lock);
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
