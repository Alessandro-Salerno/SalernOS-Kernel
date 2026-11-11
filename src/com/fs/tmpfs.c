#include <arch/info.h>
#include <dirent.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ds/hashmap.h>
#include <kernel/com/fs/pagecache.h>
#include <kernel/com/fs/tmpfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sync/rwlock.h>
#include <kernel/com/util.h>
#include <sys/stat.h>
#include <vendor/tailq.h>

// Every directory entry owns an internal reference to its node, so a node is
// only freed once it has no links. Entries get increasing readdir cookies from
// 2 (0 and 1 are "." and "..") and are kept in that order.
//
// LOCKING: directories are protected by their Vfs lock, file data by the
// file's own lock. num_links and num_subdirs are atomic, since stat reads them
// without the directory lock and a node may be linked in independent
// directories

#define TMPFS_BUCKETS  (PAGE_SIZE / sizeof(struct HashmapEntryTailQ))
#define TMPFS_MAX_SIZE (PAGECACHE_MAX_PAGES * PAGE_SIZE)

#define TMPFS_COOKIE_DOT    0
#define TMPFS_COOKIE_DOTDOT 1
#define TMPFS_COOKIE_FIRST  2

struct TmpFSNode;

struct TmpFSDirEnt {
    TAILQ_ENTRY(TmpFSDirEnt) order_link;
    struct TmpFSNode *node; // owns a reference
    u64 cookie;
    usize namelen;
    char name[];
};

TAILQ_HEAD(TmpFSDirEntList, TmpFSDirEnt);

struct TmpFSDir {
    Hashmap names; // name -> entry, no buckets until the first entry
    usize num_entries;
    usize num_subdirs;
    u64 next_cookie;
    struct TmpFSDirEntList order;
    struct TmpFSNode *parent; // NULL for the root and dead directories

    // Where the last readdir stopped, so that reading a whole directory is
    // linear. Atomic in readdir, since readers share the lock and write it
    struct TmpFSDirEnt *hint;
};

// Bytes past size are garbage in pages that exist, so growing the file shall
// zero them first
struct TmpFSFile {
    RWLock lock;
    usize size;
    usize num_pages; // no page at an index >= num_pages exists
    PageCache *pages;
};

struct TmpFSNode {
    Vnode vnode; // first member
    usize num_links;

    union {
        struct TmpFSDir dir;
        struct TmpFSFile file;
        struct {
            char *target;
            usize len;
        } link;
        void *private; // special nodes
    };
};

static VnodeOps g_tmpfs_node_ops;
static FSType g_tmpfs_type;

static inline struct TmpFSNode *tnode_of(Vnode *vnode) {
    return (struct TmpFSNode *)vnode;
}

static inline bool name_is_dotdot(const char *name, usize namelen) {
    return namelen == 2 && memcmp(name, "..", 2) == 0;
}

// DIRECTORIES

static inline bool dir_names_allocated(struct TmpFSDir *dir) {
    return dir->names.capacity != 0;
}

static inline bool dir_is_empty(struct TmpFSDir *dir) {
    return dir->num_entries == 0;
}

static inline u64 dirent_next_off(struct TmpFSDirEnt *entry) {
    return entry->cookie + 1;
}

static struct TmpFSDirEnt *dir_find(struct TmpFSDir *dir, const char *name,
                                    usize namelen) {
    struct TmpFSDirEnt *entry = NULL;
    if (dir_names_allocated(dir)) {
        hashmap_get(&dir->names, (void *)name, namelen, (void **)&entry);
    }
    return entry;
}

static int dir_insert(struct TmpFSDir *dir, struct TmpFSDirEnt *entry) {
    int ret = 0;
    if (!dir_names_allocated(dir)) {
        ret = hashmap_init(&dir->names, TMPFS_BUCKETS);
    }
    if (ret == 0) {
        ret = hashmap_put(&dir->names, entry->name, entry->namelen, entry);
    }
    if (ret != 0) return ret;

    entry->cookie = dir->next_cookie++;
    TAILQ_INSERT_TAIL(&dir->order, entry, order_link);
    dir->num_entries++;
    return 0;
}

static void dir_remove(struct TmpFSDir *dir, struct TmpFSDirEnt *entry) {
    verify(hashmap_remove(&dir->names, entry->name, entry->namelen) == 0);
    TAILQ_REMOVE(&dir->order, entry, order_link);
    dir->num_entries--;
    if (entry == dir->hint) dir->hint = NULL;
}

static struct TmpFSDirEnt *dirent_alloc(const char *name, usize namelen,
                                        struct TmpFSNode *node) {
    struct TmpFSDirEnt *entry = slab_alloc(sizeof(struct TmpFSDirEnt) +
                                           namelen);
    if (entry == NULL) return NULL;

    entry->node = node;
    entry->namelen = namelen;
    memcpy(entry->name, name, namelen);
    return entry;
}

static void dirent_free(struct TmpFSDirEnt *entry) {
    slab_free(entry, sizeof(struct TmpFSDirEnt) + entry->namelen);
}

// The entry's reference to node is up to the caller
static int dir_add(struct TmpFSDir *dir, const char *name, usize namelen,
                   struct TmpFSNode *node) {
    struct TmpFSDirEnt *entry = dirent_alloc(name, namelen, node);
    if (entry == NULL) return ENOMEM;

    int ret = dir_insert(dir, entry);
    if (ret != 0) dirent_free(entry);
    return ret;
}

// Removes entry and drops its reference to its node
static void dir_unlink(struct TmpFSNode *dir, struct TmpFSDirEnt *entry) {
    struct TmpFSNode *node = entry->node;
    dir_remove(&dir->dir, entry);
    dirent_free(entry);
    __atomic_sub_fetch(&node->num_links, 1, __ATOMIC_RELAXED);
    if (node->vnode.type == VNODE_TYPE_DIR) {
        node->dir.parent = NULL;
        __atomic_sub_fetch(&dir->dir.num_subdirs, 1, __ATOMIC_RELAXED);
    }

    vfs_vnode_rel_internal(&node->vnode);
}

// NODES

// The new node has no links and one external reference
static int node_alloc(Vfs *vfs, const VnodeAttr *attr,
                      struct TmpFSNode **out_tmpfs_node) {
    const TmpFSSpecial *special = attr->fsdata;
    *out_tmpfs_node = NULL;

    switch (attr->type) {
        case VNODE_TYPE_FILE:
        case VNODE_TYPE_DIR:
        case VNODE_TYPE_LINK:
        case VNODE_TYPE_SOCKET:
            break;
        case VNODE_TYPE_CHARDEV:
        case VNODE_TYPE_BLOCKDEV:
            // Devices only exist as special nodes
            if (special == NULL) return EPERM;
            break;
        default:
            return ENOSYS;
    }

    struct TmpFSNode *node = slab_alloc(sizeof(struct TmpFSNode));
    if (node == NULL) return ENOMEM;

    if (attr->type == VNODE_TYPE_FILE) {
        node->file.pages = pagecache_new();
        if (node->file.pages == NULL) {
            slab_free(node, sizeof(struct TmpFSNode));
            return ENOMEM;
        }
        rwlock_init(&node->file.lock);
    } else if (attr->type == VNODE_TYPE_DIR) {
        TAILQ_INIT(&node->dir.order);
        node->dir.next_cookie = TMPFS_COOKIE_FIRST;
    } else if (attr->type == VNODE_TYPE_LINK) {
        node->link.target = slab_alloc(attr->targetlen);
        if (node->link.target == NULL) {
            slab_free(node, sizeof(struct TmpFSNode));
            return ENOMEM;
        }
        memcpy(node->link.target, attr->target, attr->targetlen);
        node->link.len = attr->targetlen;
    } else if (special != NULL) {
        node->private = special->private;
    }

    vfs_vnode_init(&node->vnode, vfs, attr->type, &g_tmpfs_node_ops, node);
    *out_tmpfs_node = node;
    return 0;
}

static void node_set_special_ops(struct TmpFSNode *node,
                                 const VnodeAttr *attr) {
    if (attr->type == VNODE_TYPE_CHARDEV || attr->type == VNODE_TYPE_BLOCKDEV) {
        const TmpFSSpecial *special = attr->fsdata;
        node->vnode.ops = special->ops;
    }
}

static ino_t parent_ino(struct TmpFSNode *dir) {
    return dir->dir.parent != NULL ? dir->dir.parent->vnode.ino
                                   : dir->vnode.ino;
}

// FILE DATA

// LOCKING: file->lock held, in write mode if create
static bool file_page(struct TmpFSFile *file, usize index, bool create,
                      uptr *out_page) {
    if (!create) {
        return index < file->num_pages &&
            pagecache_get(file->pages, index, out_page);
    }

    pagecache_default(file->pages, index, out_page);
    file->num_pages = max(file->num_pages, index + 1);
    return true;
}

// LOCKING: file->lock held in write mode
static void file_zero(struct TmpFSFile *file, u64 from, u64 to) {
    u64 last = min(to, (u64)file->num_pages * PAGE_SIZE);
    for (u64 curr = from; curr < last;) {
        u64 end = min(PAGE_ROUND(curr) + PAGE_SIZE, last);
        uptr page;
        if (file_page(file, curr / PAGE_SIZE, false, &page)) {
            memset((u8 *)page + curr % PAGE_SIZE, 0, end - curr);
        }
        curr = end;
    }
}

// VNODE OPS

static int tmpfs_lookup(Vnode *dir, const char *name, usize namelen,
                        Vnode **out_vnode) {
    struct TmpFSNode *tdir = tnode_of(dir);

    if (name_is_dotdot(name, namelen)) {
        assert(tdir->dir.parent != NULL);
        *out_vnode = &tdir->dir.parent->vnode;
        vfs_vnode_hold(*out_vnode);
        return 0;
    }

    struct TmpFSDirEnt *entry = dir_find(&tdir->dir, name, namelen);
    if (entry == NULL) {
        *out_vnode = NULL;
        return ENOENT;
    }

    // The entry's reference is stable while dir is locked
    *out_vnode = &entry->node->vnode;
    vfs_vnode_hold(*out_vnode);
    return 0;
}

static int tmpfs_create(Vnode *dir, const char *name, usize namelen,
                        const VnodeAttr *attr, Vnode **out_vnode) {
    struct TmpFSNode *tdir = tnode_of(dir);
    struct TmpFSNode *node = NULL;
    *out_vnode = NULL;

    int ret = node_alloc(dir->vfs, attr, &node);
    if (ret != 0) return ret;

    ret = dir_add(&tdir->dir, name, namelen, node);
    if (ret != 0) {
        vfs_vnode_rel(&node->vnode);
        return ret;
    }

    node_set_special_ops(node, attr);

    // The reference from node_alloc goes to the caller
    node->num_links = 1;
    if (attr->type == VNODE_TYPE_DIR) {
        node->dir.parent = tdir;
        __atomic_add_fetch(&tdir->dir.num_subdirs, 1, __ATOMIC_RELAXED);
    }

    vfs_vnode_hold_internal(&node->vnode);
    *out_vnode = &node->vnode;
    return 0;
}

static int tmpfs_link(Vnode *dir, const char *name, usize namelen,
                      Vnode *node) {
    struct TmpFSNode *tdir = tnode_of(dir);
    struct TmpFSNode *tnode = tnode_of(node);
    usize num_links = __atomic_load_n(&tnode->num_links, __ATOMIC_RELAXED);

    // Only dir is locked, so an unlink in another directory may drop the last
    // link at any time. A node shall never go from 0 to 1 links
    do {
        if (num_links == 0) return ENOENT;
    } while (!__atomic_compare_exchange_n(&tnode->num_links, &num_links,
                                          num_links + 1, false,
                                          __ATOMIC_RELAXED, __ATOMIC_RELAXED));

    int ret = dir_add(&tdir->dir, name, namelen, tnode);
    if (ret != 0) {
        __atomic_sub_fetch(&tnode->num_links, 1, __ATOMIC_RELAXED);
        return ret;
    }

    vfs_vnode_hold_internal(node);
    return 0;
}

static int tmpfs_unlink(Vnode *dir, const char *name, usize namelen,
                        Vnode *node) {
    struct TmpFSNode *tdir = tnode_of(dir);
    struct TmpFSNode *tnode = tnode_of(node);
    struct TmpFSDirEnt *entry = dir_find(&tdir->dir, name, namelen);
    assert(entry != NULL && tnode == entry->node);

    if (node->type == VNODE_TYPE_DIR && !dir_is_empty(&tnode->dir)) {
        return ENOTEMPTY;
    }

    // The Vfs holds a reference, so this does not free node
    dir_unlink(tdir, entry);
    return 0;
}

static int tmpfs_rename(Vnode *srcdir, const char *srcname, usize srcnamelen,
                        Vnode *node, Vnode *dstdir, const char *dstname,
                        usize dstnamelen, Vnode *replaced) {
    struct TmpFSNode *tsrc = tnode_of(srcdir);
    struct TmpFSNode *tdst = tnode_of(dstdir);
    struct TmpFSNode *tnode = tnode_of(node);
    struct TmpFSDirEnt *srcent = dir_find(&tsrc->dir, srcname, srcnamelen);
    assert(srcent != NULL && tnode == srcent->node);
    bool is_dir = node->type == VNODE_TYPE_DIR;

    // Everything that can fail happens before the source entry is removed
    struct TmpFSDirEnt *dstent = NULL;
    if (replaced != NULL) {
        dstent = dir_find(&tdst->dir, dstname, dstnamelen);
        assert(dstent != NULL && tnode_of(replaced) == dstent->node);
        if (is_dir && !dir_is_empty(&tnode_of(replaced)->dir)) return ENOTEMPTY;
    } else {
        int ret = dir_add(&tdst->dir, dstname, dstnamelen, tnode);
        if (ret != 0) return ret;
    }

    // The source entry's reference moves to the target entry. A replaced
    // target keeps its entry, so dstname never stops existing
    dir_remove(&tsrc->dir, srcent);
    dirent_free(srcent);

    if (replaced != NULL) {
        struct TmpFSNode *trep = tnode_of(replaced);
        dstent->node = tnode;
        __atomic_sub_fetch(&trep->num_links, 1, __ATOMIC_RELAXED);
        if (is_dir) {
            trep->dir.parent = NULL;
            __atomic_sub_fetch(&tdst->dir.num_subdirs, 1, __ATOMIC_RELAXED);
        }

        // The Vfs holds a reference to replaced
        vfs_vnode_rel_internal(replaced);
    }

    // node->lock is held if its parent changes
    if (is_dir && tsrc != tdst) {
        __atomic_sub_fetch(&tsrc->dir.num_subdirs, 1, __ATOMIC_RELAXED);
        __atomic_add_fetch(&tdst->dir.num_subdirs, 1, __ATOMIC_RELAXED);
        tnode->dir.parent = tdst;
    }

    return 0;
}

static int readdir_emit(void *buf, usize buflen, VFSDirEnt *hdr,
                        const char *name, usize namelen,
                        usize *out_bytes_read) {
    usize req_size = sizeof(VFSDirEnt) + namelen + 1;
    if (buflen < req_size) return EOVERFLOW;

    hdr->reclen = req_size;
    VFSDirEnt *ud = buf;
    char nul = 0;
    int ret = memcpy_maybe_user(ud, hdr, sizeof(*hdr));
    if (ret == 0) ret = memcpy_maybe_user(ud->name, name, namelen);
    if (ret == 0) ret = memcpy_maybe_user(ud->name + namelen, &nul, 1);
    if (ret == 0) *out_bytes_read = req_size;
    return ret;
}

static unsigned char dirent_type(VnodeType type) {
    switch (type) {
        case VNODE_TYPE_FILE:
            return DT_REG;
        case VNODE_TYPE_DIR:
            return DT_DIR;
        case VNODE_TYPE_LINK:
            return DT_LNK;
        case VNODE_TYPE_CHARDEV:
            return DT_CHR;
        case VNODE_TYPE_FIFO:
            return DT_FIFO;
        case VNODE_TYPE_SOCKET:
            return DT_SOCK;
        case VNODE_TYPE_BLOCKDEV:
            return DT_BLK;
        default:
            return DT_UNKNOWN;
    }
}

static struct TmpFSDirEnt *readdir_seek(struct TmpFSDir *dir, u64 off) {
    struct TmpFSDirEnt *hint = __atomic_load_n(&dir->hint, __ATOMIC_RELAXED);
    if (hint != NULL && dirent_next_off(hint) == off) {
        return TAILQ_NEXT(hint, order_link);
    }

    struct TmpFSDirEnt *entry;
    TAILQ_FOREACH(entry, &dir->order, order_link) {
        if (entry->cookie >= off) return entry;
    }

    return NULL;
}

static int tmpfs_readdir(void *buf, usize buflen, Vnode *dir, u64 off,
                         usize *out_bytes_read, u64 *out_next_off) {
    struct TmpFSNode *tdir = tnode_of(dir);
    VFSDirEnt hdr = {0};
    hdr.type = DT_DIR;
    *out_bytes_read = 0;

    if (off == TMPFS_COOKIE_DOT) {
        hdr.ino = dir->ino;
        hdr.off = *out_next_off = TMPFS_COOKIE_DOTDOT;
        return readdir_emit(buf, buflen, &hdr, ".", 1, out_bytes_read);
    }

    if (off == TMPFS_COOKIE_DOTDOT) {
        hdr.ino = parent_ino(tdir);
        hdr.off = *out_next_off = TMPFS_COOKIE_FIRST;
        return readdir_emit(buf, buflen, &hdr, "..", 2, out_bytes_read);
    }

    struct TmpFSDirEnt *entry = readdir_seek(&tdir->dir, off);
    if (entry == NULL) {
        *out_next_off = off;
        return 0;
    }

    hdr.ino = entry->node->vnode.ino;
    hdr.type = dirent_type(entry->node->vnode.type);
    hdr.off = dirent_next_off(entry);
    int ret = readdir_emit(buf, buflen, &hdr, entry->name, entry->namelen,
                           out_bytes_read);
    if (ret == 0) {
        *out_next_off = hdr.off;
        __atomic_store_n(&tdir->dir.hint, entry, __ATOMIC_RELAXED);
    }
    return ret;
}

static int tmpfs_readlink(char *buf, usize buflen, Vnode *link,
                          usize *out_len) {
    struct TmpFSNode *tlink = tnode_of(link);
    memcpy(buf, tlink->link.target, min(buflen, tlink->link.len));
    *out_len = tlink->link.len;
    return 0;
}

static int check_file_io(Vnode *node) {
    if (node->type == VNODE_TYPE_FILE) return 0;

    return node->type == VNODE_TYPE_DIR ? EISDIR : EINVAL;
}

static int tmpfs_read(void *buf, usize buflen, Vnode *node, u64 off, u64 flags,
                      usize *out_bytes_read) {
    (void)flags;
    *out_bytes_read = 0;
    int ret = check_file_io(node);
    if (ret != 0) return ret;

    struct TmpFSFile *file = &tnode_of(node)->file;
    usize done = 0;
    rwlock_acq_read(&file->lock);

    if (off < file->size) buflen = min(buflen, file->size - off);
    else buflen = 0;

    for (u64 curr = off; curr < off + buflen;) {
        u64 end = min(PAGE_ROUND(curr) + PAGE_SIZE, off + buflen);
        u8 *dst = (u8 *)buf + (curr - off);
        uptr page;
        if (file_page(file, curr / PAGE_SIZE, false, &page)) {
            ret = memcpy_maybe_user(dst, (u8 *)page + curr % PAGE_SIZE,
                                    end - curr);
        } else {
            ret = memset_maybe_user(dst, 0, end - curr);
        }
        if (ret != 0) break;

        done += end - curr;
        curr = end;
    }

    rwlock_rel_read(&file->lock);
    *out_bytes_read = done;
    return done != 0 ? 0 : ret;
}

static int tmpfs_write(Vnode *node, void *buf, usize buflen, u64 off, u64 flags,
                       usize *out_bytes_written) {
    (void)flags;
    *out_bytes_written = 0;
    int ret = check_file_io(node);
    if (ret != 0) return ret;

    if (buflen == 0) return 0;
    if (off >= TMPFS_MAX_SIZE) return EFBIG;

    buflen = min(buflen, TMPFS_MAX_SIZE - off);
    struct TmpFSFile *file = &tnode_of(node)->file;
    usize done = 0;
    rwlock_acq_write(&file->lock);

    if (off > file->size) file_zero(file, file->size, off);

    for (u64 curr = off; curr < off + buflen;) {
        u64 end = min(PAGE_ROUND(curr) + PAGE_SIZE, off + buflen);
        uptr page;
        file_page(file, curr / PAGE_SIZE, true, &page);
        ret = memcpy_maybe_user((u8 *)page + curr % PAGE_SIZE,
                                (u8 *)buf + (curr - off), end - curr);
        if (ret != 0) break;

        done += end - curr;
        curr = end;
    }

    if (off + done > file->size) file->size = off + done;

    rwlock_rel_write(&file->lock);
    *out_bytes_written = done;
    return done != 0 ? 0 : ret;
}

static int tmpfs_truncate(Vnode *node, usize size) {
    if (node->type != VNODE_TYPE_FILE) return EINVAL;
    if (size > TMPFS_MAX_SIZE) return EFBIG;

    // Pages past the new size are kept, because shared mappings may still map
    // them and there is no reverse map
    struct TmpFSFile *file = &tnode_of(node)->file;
    rwlock_acq_write(&file->lock);
    if (size > file->size) file_zero(file, file->size, size);
    file->size = size;
    rwlock_rel_write(&file->lock);
    return 0;
}

static int tmpfs_mmap(Vnode *node, VMMContext *vmm_context, void *hint,
                      usize size, int vmm_flags, MMUFlags mmu_flags, u64 off,
                      void **out_addr) {
    if (node->type != VNODE_TYPE_FILE) return ENODEV;
    if (size == 0 || off % PAGE_SIZE != 0) return EINVAL;
    if (off >= TMPFS_MAX_SIZE || size > TMPFS_MAX_SIZE - off) return ENXIO;

    struct TmpFSFile *file = &tnode_of(node)->file;
    void *range_base = hint;
    bool shared = vmm_flags & VMM_FLAGS_SHARED;

    if (vmm_flags & VMM_FLAGS_NOHINT) {
        vmm_flags &= ~VMM_FLAGS_NOHINT;
        range_base = vmm_prealloc_range(vmm_context, VMM_RANGE_TYPE_FILE, size);
        if (range_base == NULL) return ENOMEM;
    }

    if (!shared) vmm_flags |= VMM_FLAGS_PRIVATE;

    // Only shared mappings may populate the page tree, so private ones only
    // need the lock in read mode
    if (shared) rwlock_acq_write(&file->lock);
    else rwlock_acq_read(&file->lock);

    for (u64 curr = off; curr < off + size; curr += PAGE_SIZE) {
        uptr page;
        bool present = file_page(file, curr / PAGE_SIZE, shared, &page);

        void *page_phys = NULL;
        if (present) {
            page_phys = (void *)HHDM_TO_PHYS(page);
            pmm_hold(page_phys);
        }

        vmm_map(vmm_context, range_base + (curr - off), page_phys, PAGE_SIZE,
                vmm_flags | (present ? 0 : VMM_FLAGS_ALLOCATE), mmu_flags);
    }

    if (shared) rwlock_rel_write(&file->lock);
    else rwlock_rel_read(&file->lock);

    *out_addr = (void *)((uptr)range_base + (off % PAGE_SIZE));
    return 0;
}

static int tmpfs_stat(Vnode *node, struct stat *out_stat) {
    struct TmpFSNode *tnode = tnode_of(node);
    usize num_links = __atomic_load_n(&tnode->num_links, __ATOMIC_RELAXED);
    out_stat->st_blksize = 512;
    out_stat->st_mode = 0700;
    out_stat->st_nlink = num_links;

    switch (node->type) {
        case VNODE_TYPE_FILE:
            rwlock_acq_read(&tnode->file.lock);
            out_stat->st_size = tnode->file.size;
            rwlock_rel_read(&tnode->file.lock);
            out_stat->st_blocks = (out_stat->st_size + 511) / 512;
            break;
        case VNODE_TYPE_DIR:
            // "." and the entry in the parent (the root is its own parent),
            // plus ".." of every subdirectory. A removed directory has none
            if (num_links != 0 || node->isroot) {
                out_stat->st_nlink = 2 +
                    __atomic_load_n(&tnode->dir.num_subdirs, __ATOMIC_RELAXED);
            }
            break;
        case VNODE_TYPE_LINK:
            out_stat->st_size = tnode->link.len;
            break;
        default:
            break;
    }

    return 0;
}

static VnodeOps g_tmpfs_node_ops = {.inactive = tmpfs_inactive,
                                    .lookup = tmpfs_lookup,
                                    .create = tmpfs_create,
                                    .link = tmpfs_link,
                                    .unlink = tmpfs_unlink,
                                    .rename = tmpfs_rename,
                                    .readdir = tmpfs_readdir,
                                    .readlink = tmpfs_readlink,
                                    .read = tmpfs_read,
                                    .write = tmpfs_write,
                                    .truncate = tmpfs_truncate,
                                    .mmap = tmpfs_mmap,
                                    .stat = tmpfs_stat};

// FILE SYSTEM OPS

// Removes every entry bottom up, without recursion
static int tmpfs_unmount(Vfs *vfs) {
    struct TmpFSNode *root = tnode_of(vfs->root);
    struct TmpFSNode *curr = root;

    for (;;) {
        struct TmpFSDirEnt *entry = TAILQ_FIRST(&curr->dir.order);
        if (entry == NULL) {
            if (root == curr) break;
            curr = curr->dir.parent;
            continue;
        }

        struct TmpFSNode *child = entry->node;
        if (child->vnode.type == VNODE_TYPE_DIR && !dir_is_empty(&child->dir)) {
            curr = child;
            continue;
        }

        dir_unlink(curr, entry);
    }

    // External references, so vfs is only freed after they are dropped
    vfs_vnode_rel(&root->vnode);
    vfs_vnode_rel(&root->vnode);
    assert(vfs->num_ext == 0);
    slab_free(vfs, sizeof(Vfs));
    return 0;
}

static int tmpfs_op_mount(Vnode *backing, u64 flags, Vfs **out_vfs) {
    (void)backing;
    (void)flags;
    *out_vfs = NULL;
    Vfs *vfs = slab_alloc(sizeof(Vfs));
    if (vfs == NULL) return ENOMEM;

    vfs_init_vfs(vfs, &g_tmpfs_type, NULL);
    struct TmpFSNode *root = NULL;
    int ret = node_alloc(vfs, &VFS_ATTR(VNODE_TYPE_DIR), &root);
    if (ret != 0) {
        slab_free(vfs, sizeof(Vfs));
        return ret;
    }

    vfs_set_root(vfs, &root->vnode);
    *out_vfs = vfs;
    return 0;
}

static FSType g_tmpfs_type = {.name = "tmpfs",
                              .mount = tmpfs_op_mount,
                              .unmount = tmpfs_unmount};

// INTERFACE

void tmpfs_init(void) { verify(vfs_register_type(&g_tmpfs_type) == 0); }

int tmpfs_mount(Vnode *mountpoint, Vfs **out_vfs) {
    return vfs_mount(&g_tmpfs_type, NULL, mountpoint, 0, out_vfs);
}

int tmpfs_new_orphan(Vfs *vfs, const VnodeAttr *attr, Vnode **out_vnode) {
    assert(&g_tmpfs_type == vfs->type);
    if (attr->type == VNODE_TYPE_DIR) {
        *out_vnode = NULL;
        return EINVAL;
    }

    struct TmpFSNode *node = NULL;
    int ret = node_alloc(vfs, attr, &node);
    if (ret == 0) node_set_special_ops(node, attr);
    *out_vnode = ret == 0 ? &node->vnode : NULL;
    return ret;
}

void *tmpfs_private(Vnode *vnode) { return tnode_of(vnode)->private; }

nlink_t tmpfs_nlink(Vnode *vnode) {
    return __atomic_load_n(&tnode_of(vnode)->num_links, __ATOMIC_RELAXED);
}

void tmpfs_inactive(Vnode *vnode) {
    struct TmpFSNode *node = tnode_of(vnode);
    assert(node->num_links == 0);

    switch (vnode->type) {
        case VNODE_TYPE_FILE:
            pagecache_free(node->file.pages);
            break;
        case VNODE_TYPE_DIR:
            assert(dir_is_empty(&node->dir));
            if (dir_names_allocated(&node->dir)) {
                hashmap_destroy(&node->dir.names);
            }
            break;
        case VNODE_TYPE_LINK:
            slab_free(node->link.target, node->link.len);
            break;
        default:
            break;
    }

    slab_free(node, sizeof(struct TmpFSNode));
}
