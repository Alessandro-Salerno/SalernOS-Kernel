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

#include <arch/info.h>
#include <dirent.h>
#include <errno.h>
#include <kernel/com/fs/pagecache.h>
#include <kernel/com/fs/tmpfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <lib/hashmap.h>
#include <lib/mem.h>
#include <lib/rwlock.h>
#include <lib/usercopy.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <vendor/tailq.h>

// Every directory entry owns an internal reference to its node, so a node is
// only freed once it has no links. Entries get increasing readdir cookies from
// 2 (0 and 1 are "." and "..") and are kept in that order.
//
// LOCKING: directories are protected by their VFS lock, file data by the
// file's own lock. num_links and num_subdirs are atomic, since stat reads them
// without the directory lock and a node may be linked in independent
// directories

#define TMPFS_BUCKETS  (ARCH_PAGE_SIZE / sizeof(struct khashmap_entry_tailq))
#define TMPFS_MAX_SIZE (COM_FS_PAGECACHE_MAX_PAGES * ARCH_PAGE_SIZE)

#define TMPFS_COOKIE_DOT    0
#define TMPFS_COOKIE_DOTDOT 1
#define TMPFS_COOKIE_FIRST  2

struct tmpfs_node;

struct tmpfs_dirent {
    TAILQ_ENTRY(tmpfs_dirent) order_link;
    struct tmpfs_node *node; // owns a reference
    uintmax_t          cookie;
    size_t             namelen;
    char               name[];
};

TAILQ_HEAD(tmpfs_dirent_list, tmpfs_dirent);

struct tmpfs_dir {
    khashmap_t names; // name -> entry, no buckets until the first entry
    size_t     num_entries;
    size_t     num_subdirs;
    uintmax_t  next_cookie;
    struct tmpfs_dirent_list order;
    struct tmpfs_node       *parent; // NULL for the root and dead directories

    // Where the last readdir stopped, so that reading a whole directory is
    // linear. Atomic in readdir, since readers share the lock and write it
    struct tmpfs_dirent *hint;
};

// Bytes past size are garbage in pages that exist, so growing the file shall
// zero them first
struct tmpfs_file {
    krwlock_t        lock;
    size_t           size;
    size_t           num_pages; // no page at an index >= num_pages exists
    com_pagecache_t *pages;
};

struct tmpfs_node {
    com_vnode_t vnode; // first member
    size_t      num_links;

    union {
        struct tmpfs_dir  dir;
        struct tmpfs_file file;
        struct {
            char  *target;
            size_t len;
        } link;
        void *private; // special nodes
    };
};

static com_vnode_ops_t TmpfsNodeOps;
static com_fs_type_t   TmpfsType;

static inline struct tmpfs_node *tnode_of(com_vnode_t *vnode) {
    return (struct tmpfs_node *)vnode;
}

static inline bool name_is_dotdot(const char *name, size_t namelen) {
    return 2 == namelen && 0 == kmemcmp(name, "..", 2);
}

// DIRECTORIES

static inline bool dir_names_allocated(struct tmpfs_dir *dir) {
    return 0 != dir->names.capacity;
}

static inline bool dir_is_empty(struct tmpfs_dir *dir) {
    return 0 == dir->num_entries;
}

static inline uintmax_t dirent_next_off(struct tmpfs_dirent *entry) {
    return entry->cookie + 1;
}

static struct tmpfs_dirent *
dir_find(struct tmpfs_dir *dir, const char *name, size_t namelen) {
    struct tmpfs_dirent *entry = NULL;
    if (dir_names_allocated(dir)) {
        khashmap_get((void **)&entry, &dir->names, (void *)name, namelen);
    }
    return entry;
}

static int dir_insert(struct tmpfs_dir *dir, struct tmpfs_dirent *entry) {
    int ret = 0;
    if (!dir_names_allocated(dir)) {
        ret = khashmap_init(&dir->names, TMPFS_BUCKETS);
    }
    if (0 == ret) {
        ret = khashmap_put(&dir->names, entry->name, entry->namelen, entry);
    }
    if (0 != ret) {
        return ret;
    }

    entry->cookie = dir->next_cookie++;
    TAILQ_INSERT_TAIL(&dir->order, entry, order_link);
    dir->num_entries++;
    return 0;
}

static void dir_remove(struct tmpfs_dir *dir, struct tmpfs_dirent *entry) {
    KASSERT_CALL(0,
                 ==,
                 khashmap_remove(&dir->names, entry->name, entry->namelen));
    TAILQ_REMOVE(&dir->order, entry, order_link);
    dir->num_entries--;
    if (entry == dir->hint) {
        dir->hint = NULL;
    }
}

static struct tmpfs_dirent *
dirent_alloc(const char *name, size_t namelen, struct tmpfs_node *node) {
    struct tmpfs_dirent *entry = com_mm_slab_alloc(sizeof(struct tmpfs_dirent) +
                                                   namelen);
    if (NULL == entry) {
        return NULL;
    }

    entry->node    = node;
    entry->namelen = namelen;
    kmemcpy(entry->name, name, namelen);
    return entry;
}

static void dirent_free(struct tmpfs_dirent *entry) {
    com_mm_slab_free(entry, sizeof(struct tmpfs_dirent) + entry->namelen);
}

// The entry's reference to node is up to the caller
static int dir_add(struct tmpfs_dir  *dir,
                   const char        *name,
                   size_t             namelen,
                   struct tmpfs_node *node) {
    struct tmpfs_dirent *entry = dirent_alloc(name, namelen, node);
    if (NULL == entry) {
        return ENOMEM;
    }

    int ret = dir_insert(dir, entry);
    if (0 != ret) {
        dirent_free(entry);
    }
    return ret;
}

// Removes entry and drops its reference to its node
static void dir_unlink(struct tmpfs_node *dir, struct tmpfs_dirent *entry) {
    struct tmpfs_node *node = entry->node;
    dir_remove(&dir->dir, entry);
    dirent_free(entry);
    __atomic_sub_fetch(&node->num_links, 1, __ATOMIC_RELAXED);
    if (E_COM_VNODE_TYPE_DIR == node->vnode.type) {
        node->dir.parent = NULL;
        __atomic_sub_fetch(&dir->dir.num_subdirs, 1, __ATOMIC_RELAXED);
    }

    COM_FS_VFS_VNODE_RELEASE_INTERNAL(&node->vnode);
}

// NODES

// The new node has no links and one external reference
static int node_alloc(struct tmpfs_node     **out,
                      com_vfs_t              *vfs,
                      const com_vnode_attr_t *attr) {
    const com_fs_tmpfs_special_t *special = attr->fsdata;
    *out                                  = NULL;

    switch (attr->type) {
        case E_COM_VNODE_TYPE_FILE:
        case E_COM_VNODE_TYPE_DIR:
        case E_COM_VNODE_TYPE_LINK:
        case E_COM_VNODE_TYPE_SOCKET:
            break;
        case E_COM_VNODE_TYPE_CHARDEV:
        case E_COM_VNODE_TYPE_BLOCKDEV:
            // Devices only exist as special nodes
            if (NULL == special) {
                return EPERM;
            }
            break;
        default:
            return ENOSYS;
    }

    struct tmpfs_node *node = com_mm_slab_alloc(sizeof(struct tmpfs_node));
    if (NULL == node) {
        return ENOMEM;
    }

    if (E_COM_VNODE_TYPE_FILE == attr->type) {
        node->file.pages = com_fs_pagecache_new();
        if (NULL == node->file.pages) {
            com_mm_slab_free(node, sizeof(struct tmpfs_node));
            return ENOMEM;
        }
        KRWLOCK_INIT(&node->file.lock);
    } else if (E_COM_VNODE_TYPE_DIR == attr->type) {
        TAILQ_INIT(&node->dir.order);
        node->dir.next_cookie = TMPFS_COOKIE_FIRST;
    } else if (E_COM_VNODE_TYPE_LINK == attr->type) {
        node->link.target = com_mm_slab_alloc(attr->targetlen);
        if (NULL == node->link.target) {
            com_mm_slab_free(node, sizeof(struct tmpfs_node));
            return ENOMEM;
        }
        kmemcpy(node->link.target, attr->target, attr->targetlen);
        node->link.len = attr->targetlen;
    } else if (NULL != special) {
        node->private = special->private;
    }

    com_fs_vfs_vnode_init(&node->vnode, vfs, attr->type, &TmpfsNodeOps, node);
    *out = node;
    return 0;
}

static void node_set_special_ops(struct tmpfs_node      *node,
                                 const com_vnode_attr_t *attr) {
    if (E_COM_VNODE_TYPE_CHARDEV == attr->type ||
        E_COM_VNODE_TYPE_BLOCKDEV == attr->type) {
        const com_fs_tmpfs_special_t *special = attr->fsdata;
        node->vnode.ops                       = special->ops;
    }
}

static ino_t parent_ino(struct tmpfs_node *dir) {
    return NULL != dir->dir.parent ? dir->dir.parent->vnode.ino
                                   : dir->vnode.ino;
}

// FILE DATA

// LOCKING: file->lock held, in write mode if create
static bool
file_page(uintptr_t *page, struct tmpfs_file *file, size_t index, bool create) {
    if (!create) {
        return index < file->num_pages &&
               com_fs_pagecache_get(page, file->pages, index);
    }

    com_fs_pagecache_default(page, file->pages, index);
    file->num_pages = KMAX(file->num_pages, index + 1);
    return true;
}

// LOCKING: file->lock held in write mode
static void file_zero(struct tmpfs_file *file, uintmax_t from, uintmax_t to) {
    uintmax_t last = KMIN(to, (uintmax_t)file->num_pages * ARCH_PAGE_SIZE);
    for (uintmax_t curr = from; curr < last;) {
        uintmax_t end = KMIN(ARCH_PAGE_ROUND(curr) + ARCH_PAGE_SIZE, last);
        uintptr_t page;
        if (file_page(&page, file, curr / ARCH_PAGE_SIZE, false)) {
            kmemset((uint8_t *)page + curr % ARCH_PAGE_SIZE, end - curr, 0);
        }
        curr = end;
    }
}

// VNODE OPS

static int tmpfs_lookup(com_vnode_t **out,
                        com_vnode_t  *dir,
                        const char   *name,
                        size_t        namelen) {
    struct tmpfs_node *tdir = tnode_of(dir);

    if (name_is_dotdot(name, namelen)) {
        KASSERT(NULL != tdir->dir.parent);
        *out = &tdir->dir.parent->vnode;
        COM_FS_VFS_VNODE_HOLD(*out);
        return 0;
    }

    struct tmpfs_dirent *entry = dir_find(&tdir->dir, name, namelen);
    if (NULL == entry) {
        *out = NULL;
        return ENOENT;
    }

    // The entry's reference is stable while dir is locked
    *out = &entry->node->vnode;
    COM_FS_VFS_VNODE_HOLD(*out);
    return 0;
}

static int tmpfs_create(com_vnode_t           **out,
                        com_vnode_t            *dir,
                        const char             *name,
                        size_t                  namelen,
                        const com_vnode_attr_t *attr) {
    struct tmpfs_node *tdir = tnode_of(dir);
    struct tmpfs_node *node = NULL;
    *out                    = NULL;

    int ret = node_alloc(&node, dir->vfs, attr);
    if (0 != ret) {
        return ret;
    }

    ret = dir_add(&tdir->dir, name, namelen, node);
    if (0 != ret) {
        COM_FS_VFS_VNODE_RELEASE(&node->vnode);
        return ret;
    }

    node_set_special_ops(node, attr);

    // The reference from node_alloc goes to the caller
    node->num_links = 1;
    if (E_COM_VNODE_TYPE_DIR == attr->type) {
        node->dir.parent = tdir;
        __atomic_add_fetch(&tdir->dir.num_subdirs, 1, __ATOMIC_RELAXED);
    }

    COM_FS_VFS_VNODE_HOLD_INTERNAL(&node->vnode);
    *out = &node->vnode;
    return 0;
}

static int tmpfs_link(com_vnode_t *dir,
                      const char  *name,
                      size_t       namelen,
                      com_vnode_t *node) {
    struct tmpfs_node *tdir  = tnode_of(dir);
    struct tmpfs_node *tnode = tnode_of(node);
    size_t num_links = __atomic_load_n(&tnode->num_links, __ATOMIC_RELAXED);

    // Only dir is locked, so an unlink in another directory may drop the last
    // link at any time. A node shall never go from 0 to 1 links
    do {
        if (0 == num_links) {
            return ENOENT;
        }
    } while (!__atomic_compare_exchange_n(&tnode->num_links,
                                          &num_links,
                                          num_links + 1,
                                          false,
                                          __ATOMIC_RELAXED,
                                          __ATOMIC_RELAXED));

    int ret = dir_add(&tdir->dir, name, namelen, tnode);
    if (0 != ret) {
        __atomic_sub_fetch(&tnode->num_links, 1, __ATOMIC_RELAXED);
        return ret;
    }

    COM_FS_VFS_VNODE_HOLD_INTERNAL(node);
    return 0;
}

static int tmpfs_unlink(com_vnode_t *dir,
                        const char  *name,
                        size_t       namelen,
                        com_vnode_t *node) {
    struct tmpfs_node   *tdir  = tnode_of(dir);
    struct tmpfs_node   *tnode = tnode_of(node);
    struct tmpfs_dirent *entry = dir_find(&tdir->dir, name, namelen);
    KASSERT(NULL != entry && tnode == entry->node);

    if (E_COM_VNODE_TYPE_DIR == node->type && !dir_is_empty(&tnode->dir)) {
        return ENOTEMPTY;
    }

    // The VFS holds a reference, so this does not free node
    dir_unlink(tdir, entry);
    return 0;
}

static int tmpfs_rename(com_vnode_t *srcdir,
                        const char  *srcname,
                        size_t       srcnamelen,
                        com_vnode_t *node,
                        com_vnode_t *dstdir,
                        const char  *dstname,
                        size_t       dstnamelen,
                        com_vnode_t *replaced) {
    struct tmpfs_node   *tsrc   = tnode_of(srcdir);
    struct tmpfs_node   *tdst   = tnode_of(dstdir);
    struct tmpfs_node   *tnode  = tnode_of(node);
    struct tmpfs_dirent *srcent = dir_find(&tsrc->dir, srcname, srcnamelen);
    KASSERT(NULL != srcent && tnode == srcent->node);
    bool is_dir = E_COM_VNODE_TYPE_DIR == node->type;

    // Everything that can fail happens before the source entry is removed
    struct tmpfs_dirent *dstent = NULL;
    if (NULL != replaced) {
        dstent = dir_find(&tdst->dir, dstname, dstnamelen);
        KASSERT(NULL != dstent && tnode_of(replaced) == dstent->node);
        if (is_dir && !dir_is_empty(&tnode_of(replaced)->dir)) {
            return ENOTEMPTY;
        }
    } else {
        int ret = dir_add(&tdst->dir, dstname, dstnamelen, tnode);
        if (0 != ret) {
            return ret;
        }
    }

    // The source entry's reference moves to the target entry. A replaced
    // target keeps its entry, so dstname never stops existing
    dir_remove(&tsrc->dir, srcent);
    dirent_free(srcent);

    if (NULL != replaced) {
        struct tmpfs_node *trep = tnode_of(replaced);
        dstent->node            = tnode;
        __atomic_sub_fetch(&trep->num_links, 1, __ATOMIC_RELAXED);
        if (is_dir) {
            trep->dir.parent = NULL;
            __atomic_sub_fetch(&tdst->dir.num_subdirs, 1, __ATOMIC_RELAXED);
        }

        // The VFS holds a reference to replaced
        COM_FS_VFS_VNODE_RELEASE_INTERNAL(replaced);
    }

    // node->lock is held if its parent changes
    if (is_dir && tsrc != tdst) {
        __atomic_sub_fetch(&tsrc->dir.num_subdirs, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&tdst->dir.num_subdirs, 1, __ATOMIC_RELAXED);
        tnode->dir.parent = tdst;
    }

    return 0;
}

static int readdir_emit(size_t       *bytes_read,
                        void         *buf,
                        size_t        buflen,
                        com_dirent_t *hdr,
                        const char   *name,
                        size_t        namelen) {
    size_t req_size = sizeof(com_dirent_t) + namelen + 1;
    if (buflen < req_size) {
        return EOVERFLOW;
    }

    hdr->reclen       = req_size;
    com_dirent_t *ud  = buf;
    char          nul = 0;
    int           ret = kmemcpy_maybe_user(ud, hdr, sizeof(*hdr));
    if (0 == ret) {
        ret = kmemcpy_maybe_user(ud->name, name, namelen);
    }
    if (0 == ret) {
        ret = kmemcpy_maybe_user(ud->name + namelen, &nul, 1);
    }
    if (0 == ret) {
        *bytes_read = req_size;
    }
    return ret;
}

static unsigned char dirent_type(com_vnode_type_t type) {
    switch (type) {
        case E_COM_VNODE_TYPE_FILE:
            return DT_REG;
        case E_COM_VNODE_TYPE_DIR:
            return DT_DIR;
        case E_COM_VNODE_TYPE_LINK:
            return DT_LNK;
        case E_COM_VNODE_TYPE_CHARDEV:
            return DT_CHR;
        case E_COM_VNODE_TYPE_FIFO:
            return DT_FIFO;
        case E_COM_VNODE_TYPE_SOCKET:
            return DT_SOCK;
        case E_COM_VNODE_TYPE_BLOCKDEV:
            return DT_BLK;
        default:
            return DT_UNKNOWN;
    }
}

static struct tmpfs_dirent *readdir_seek(struct tmpfs_dir *dir, uintmax_t off) {
    struct tmpfs_dirent *hint = __atomic_load_n(&dir->hint, __ATOMIC_RELAXED);
    if (NULL != hint && dirent_next_off(hint) == off) {
        return TAILQ_NEXT(hint, order_link);
    }

    struct tmpfs_dirent *entry;
    TAILQ_FOREACH(entry, &dir->order, order_link) {
        if (entry->cookie >= off) {
            return entry;
        }
    }

    return NULL;
}

static int tmpfs_readdir(void        *buf,
                         size_t       buflen,
                         size_t      *bytes_read,
                         uintmax_t   *next_off,
                         com_vnode_t *dir,
                         uintmax_t    off) {
    struct tmpfs_node *tdir = tnode_of(dir);
    com_dirent_t       hdr  = {0};
    hdr.type                = DT_DIR;
    *bytes_read             = 0;

    if (TMPFS_COOKIE_DOT == off) {
        hdr.ino = dir->ino;
        hdr.off = *next_off = TMPFS_COOKIE_DOTDOT;
        return readdir_emit(bytes_read, buf, buflen, &hdr, ".", 1);
    }

    if (TMPFS_COOKIE_DOTDOT == off) {
        hdr.ino = parent_ino(tdir);
        hdr.off = *next_off = TMPFS_COOKIE_FIRST;
        return readdir_emit(bytes_read, buf, buflen, &hdr, "..", 2);
    }

    struct tmpfs_dirent *entry = readdir_seek(&tdir->dir, off);
    if (NULL == entry) {
        *next_off = off;
        return 0;
    }

    hdr.ino  = entry->node->vnode.ino;
    hdr.type = dirent_type(entry->node->vnode.type);
    hdr.off  = dirent_next_off(entry);
    int ret  = readdir_emit(bytes_read,
                            buf,
                            buflen,
                            &hdr,
                            entry->name,
                            entry->namelen);
    if (0 == ret) {
        *next_off = hdr.off;
        __atomic_store_n(&tdir->dir.hint, entry, __ATOMIC_RELAXED);
    }
    return ret;
}

static int
tmpfs_readlink(size_t *len, char *buf, size_t buflen, com_vnode_t *link) {
    struct tmpfs_node *tlink = tnode_of(link);
    kmemcpy(buf, tlink->link.target, KMIN(buflen, tlink->link.len));
    *len = tlink->link.len;
    return 0;
}

static int check_file_io(com_vnode_t *node) {
    if (E_COM_VNODE_TYPE_FILE == node->type) {
        return 0;
    }

    return E_COM_VNODE_TYPE_DIR == node->type ? EISDIR : EINVAL;
}

static int tmpfs_read(void        *buf,
                      size_t       buflen,
                      size_t      *bytes_read,
                      com_vnode_t *node,
                      uintmax_t    off,
                      uintmax_t    flags) {
    (void)flags;
    *bytes_read = 0;
    int ret     = check_file_io(node);
    if (0 != ret) {
        return ret;
    }

    struct tmpfs_file *file = &tnode_of(node)->file;
    size_t             done = 0;
    krwlock_acquire_read(&file->lock);

    if (off < file->size) {
        buflen = KMIN(buflen, file->size - off);
    } else {
        buflen = 0;
    }

    for (uintmax_t curr = off; curr < off + buflen;) {
        uintmax_t end = KMIN(ARCH_PAGE_ROUND(curr) + ARCH_PAGE_SIZE,
                             off + buflen);
        uint8_t  *dst = (uint8_t *)buf + (curr - off);
        uintptr_t page;
        if (file_page(&page, file, curr / ARCH_PAGE_SIZE, false)) {
            ret = kmemcpy_maybe_user(dst,
                                     (uint8_t *)page + curr % ARCH_PAGE_SIZE,
                                     end - curr);
        } else {
            ret = kmemset_maybe_user(dst, end - curr, 0);
        }
        if (0 != ret) {
            break;
        }

        done += end - curr;
        curr = end;
    }

    krwlock_release_read(&file->lock);
    *bytes_read = done;
    return 0 != done ? 0 : ret;
}

static int tmpfs_write(size_t      *bytes_written,
                       com_vnode_t *node,
                       void        *buf,
                       size_t       buflen,
                       uintmax_t    off,
                       uintmax_t    flags) {
    (void)flags;
    *bytes_written = 0;
    int ret        = check_file_io(node);
    if (0 != ret) {
        return ret;
    }

    if (0 == buflen) {
        return 0;
    }

    if (off >= TMPFS_MAX_SIZE) {
        return EFBIG;
    }

    buflen                  = KMIN(buflen, TMPFS_MAX_SIZE - off);
    struct tmpfs_file *file = &tnode_of(node)->file;
    size_t             done = 0;
    krwlock_acquire_write(&file->lock);

    if (off > file->size) {
        file_zero(file, file->size, off);
    }

    for (uintmax_t curr = off; curr < off + buflen;) {
        uintmax_t end = KMIN(ARCH_PAGE_ROUND(curr) + ARCH_PAGE_SIZE,
                             off + buflen);
        uintptr_t page;
        file_page(&page, file, curr / ARCH_PAGE_SIZE, true);
        ret = kmemcpy_maybe_user((uint8_t *)page + curr % ARCH_PAGE_SIZE,
                                 (uint8_t *)buf + (curr - off),
                                 end - curr);
        if (0 != ret) {
            break;
        }

        done += end - curr;
        curr = end;
    }

    if (off + done > file->size) {
        file->size = off + done;
    }

    krwlock_release_write(&file->lock);
    *bytes_written = done;
    return 0 != done ? 0 : ret;
}

static int tmpfs_truncate(com_vnode_t *node, size_t size) {
    if (E_COM_VNODE_TYPE_FILE != node->type) {
        return EINVAL;
    }

    if (size > TMPFS_MAX_SIZE) {
        return EFBIG;
    }

    // Pages past the new size are kept, because shared mappings may still map
    // them and there is no reverse map
    struct tmpfs_file *file = &tnode_of(node)->file;
    krwlock_acquire_write(&file->lock);
    if (size > file->size) {
        file_zero(file, file->size, size);
    }
    file->size = size;
    krwlock_release_write(&file->lock);
    return 0;
}

static int tmpfs_mmap(void             **out,
                      com_vnode_t       *node,
                      com_vmm_context_t *vmm_context,
                      void              *hint,
                      size_t             size,
                      int                vmm_flags,
                      arch_mmu_flags_t   mmu_flags,
                      uintmax_t          off) {
    if (E_COM_VNODE_TYPE_FILE != node->type) {
        return ENODEV;
    }

    if (0 == size || 0 != off % ARCH_PAGE_SIZE) {
        return EINVAL;
    }

    if (off >= TMPFS_MAX_SIZE || size > TMPFS_MAX_SIZE - off) {
        return ENXIO;
    }

    struct tmpfs_file *file       = &tnode_of(node)->file;
    void              *range_base = hint;
    bool               shared     = COM_MM_VMM_FLAGS_SHARED & vmm_flags;

    if (COM_MM_VMM_FLAGS_NOHINT & vmm_flags) {
        vmm_flags &= ~COM_MM_VMM_FLAGS_NOHINT;
        range_base = com_mm_vmm_prealloc_range(vmm_context,
                                               E_COM_VMM_RANGE_TYPE_FILE,
                                               size);
        if (NULL == range_base) {
            return ENOMEM;
        }
    }

    if (!shared) {
        vmm_flags |= COM_MM_VMM_FLAGS_PRIVATE;
    }

    // Only shared mappings may populate the page tree, so private ones only
    // need the lock in read mode
    if (shared) {
        krwlock_acquire_write(&file->lock);
    } else {
        krwlock_acquire_read(&file->lock);
    }

    for (uintmax_t curr = off; curr < off + size; curr += ARCH_PAGE_SIZE) {
        uintptr_t page;
        bool present = file_page(&page, file, curr / ARCH_PAGE_SIZE, shared);

        void *page_phys = NULL;
        if (present) {
            page_phys = (void *)ARCH_HHDM_TO_PHYS(page);
            com_mm_pmm_hold(page_phys);
        }

        com_mm_vmm_map(vmm_context,
                       range_base + (curr - off),
                       page_phys,
                       ARCH_PAGE_SIZE,
                       vmm_flags | (present ? 0 : COM_MM_VMM_FLAGS_ALLOCATE),
                       mmu_flags);
    }

    if (shared) {
        krwlock_release_write(&file->lock);
    } else {
        krwlock_release_read(&file->lock);
    }

    *out = (void *)((uintptr_t)range_base + (off % ARCH_PAGE_SIZE));
    return 0;
}

static int tmpfs_stat(struct stat *out, com_vnode_t *node) {
    struct tmpfs_node *tnode = tnode_of(node);
    size_t num_links = __atomic_load_n(&tnode->num_links, __ATOMIC_RELAXED);
    out->st_blksize  = 512;
    out->st_mode     = 0700;
    out->st_nlink    = num_links;

    switch (node->type) {
        case E_COM_VNODE_TYPE_FILE:
            krwlock_acquire_read(&tnode->file.lock);
            out->st_size = tnode->file.size;
            krwlock_release_read(&tnode->file.lock);
            out->st_blocks = (out->st_size + 511) / 512;
            break;
        case E_COM_VNODE_TYPE_DIR:
            // "." and the entry in the parent (the root is its own parent),
            // plus ".." of every subdirectory. A removed directory has none
            if (0 != num_links || node->isroot) {
                out->st_nlink = 2 + __atomic_load_n(&tnode->dir.num_subdirs,
                                                    __ATOMIC_RELAXED);
            }
            break;
        case E_COM_VNODE_TYPE_LINK:
            out->st_size = tnode->link.len;
            break;
        default:
            break;
    }

    return 0;
}

static com_vnode_ops_t TmpfsNodeOps = {.inactive = com_fs_tmpfs_inactive,
                                       .lookup   = tmpfs_lookup,
                                       .create   = tmpfs_create,
                                       .link     = tmpfs_link,
                                       .unlink   = tmpfs_unlink,
                                       .rename   = tmpfs_rename,
                                       .readdir  = tmpfs_readdir,
                                       .readlink = tmpfs_readlink,
                                       .read     = tmpfs_read,
                                       .write    = tmpfs_write,
                                       .truncate = tmpfs_truncate,
                                       .mmap     = tmpfs_mmap,
                                       .stat     = tmpfs_stat};

// FILE SYSTEM OPS

// Removes every entry bottom up, without recursion
static int tmpfs_unmount(com_vfs_t *vfs) {
    struct tmpfs_node *root = tnode_of(vfs->root);
    struct tmpfs_node *curr = root;

    for (;;) {
        struct tmpfs_dirent *entry = TAILQ_FIRST(&curr->dir.order);
        if (NULL == entry) {
            if (root == curr) {
                break;
            }
            curr = curr->dir.parent;
            continue;
        }

        struct tmpfs_node *child = entry->node;
        if (E_COM_VNODE_TYPE_DIR == child->vnode.type &&
            !dir_is_empty(&child->dir)) {
            curr = child;
            continue;
        }

        dir_unlink(curr, entry);
    }

    // External references, so vfs is only freed after they are dropped
    COM_FS_VFS_VNODE_RELEASE(&root->vnode);
    COM_FS_VFS_VNODE_RELEASE(&root->vnode);
    KASSERT(0 == vfs->num_ext);
    com_mm_slab_free(vfs, sizeof(com_vfs_t));
    return 0;
}

static int tmpfs_mount(com_vfs_t **out, com_vnode_t *backing, uintmax_t flags) {
    (void)backing;
    (void)flags;
    *out           = NULL;
    com_vfs_t *vfs = com_mm_slab_alloc(sizeof(com_vfs_t));
    if (NULL == vfs) {
        return ENOMEM;
    }

    com_fs_vfs_init_vfs(vfs, &TmpfsType, NULL);
    struct tmpfs_node *root = NULL;
    int ret = node_alloc(&root, vfs, &COM_FS_VFS_ATTR(E_COM_VNODE_TYPE_DIR));
    if (0 != ret) {
        com_mm_slab_free(vfs, sizeof(com_vfs_t));
        return ret;
    }

    com_fs_vfs_set_root(vfs, &root->vnode);
    *out = vfs;
    return 0;
}

static com_fs_type_t TmpfsType = {.name    = "tmpfs",
                                  .mount   = tmpfs_mount,
                                  .unmount = tmpfs_unmount};

// INTERFACE

void com_fs_tmpfs_init(void) {
    KASSERT_CALL(0, ==, com_fs_vfs_register_type(&TmpfsType));
}

int com_fs_tmpfs_mount(com_vfs_t **out, com_vnode_t *mountpoint) {
    return com_fs_vfs_mount(out, &TmpfsType, NULL, mountpoint, 0);
}

int com_fs_tmpfs_new_orphan(com_vnode_t           **out,
                            com_vfs_t              *vfs,
                            const com_vnode_attr_t *attr) {
    KASSERT(&TmpfsType == vfs->type);
    if (E_COM_VNODE_TYPE_DIR == attr->type) {
        *out = NULL;
        return EINVAL;
    }

    struct tmpfs_node *node = NULL;
    int                ret  = node_alloc(&node, vfs, attr);
    if (0 == ret) {
        node_set_special_ops(node, attr);
    }
    *out = 0 == ret ? &node->vnode : NULL;
    return ret;
}

void *com_fs_tmpfs_private(com_vnode_t *vnode) {
    return tnode_of(vnode)->private;
}

nlink_t com_fs_tmpfs_nlink(com_vnode_t *vnode) {
    return __atomic_load_n(&tnode_of(vnode)->num_links, __ATOMIC_RELAXED);
}

void com_fs_tmpfs_inactive(com_vnode_t *vnode) {
    struct tmpfs_node *node = tnode_of(vnode);
    KASSERT(0 == node->num_links);

    switch (vnode->type) {
        case E_COM_VNODE_TYPE_FILE:
            com_fs_pagecache_free(node->file.pages);
            break;
        case E_COM_VNODE_TYPE_DIR:
            KASSERT(dir_is_empty(&node->dir));
            if (dir_names_allocated(&node->dir)) {
                khashmap_destroy(&node->dir.names);
            }
            break;
        case E_COM_VNODE_TYPE_LINK:
            com_mm_slab_free(node->link.target, node->link.len);
            break;
        default:
            break;
    }

    com_mm_slab_free(node, sizeof(struct tmpfs_node));
}
