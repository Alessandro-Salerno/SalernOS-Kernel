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
#include <errno.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/slab.h>
#include <lib/hashmap.h>
#include <lib/mem.h>
#include <lib/mutex.h>
#include <lib/rwlock.h>
#include <lib/spinlock.h>
#include <lib/str.h>
#include <lib/sync.h>
#include <lib/util.h>
#include <stdbool.h>
#include <stdint.h>
#include <vendor/tailq.h>

static TAILQ_HEAD(, com_fs_type) Types = TAILQ_HEAD_INITIALIZER(Types);
static kspinlock_t TypesLock = KSPINLOCK_NEW();

// An empty name is what is left of a path after its last slash
static inline bool is_dot(const char *name, size_t namelen) {
    return 0 == namelen || (1 == namelen && '.' == name[0]);
}

static inline bool is_dotdot(const char *name, size_t namelen) {
    return 2 == namelen && 0 == kmemcmp(name, "..", 2);
}

static inline bool is_dot_name(const char *name, size_t namelen) {
    KASSERT(NULL == kmemchr(name, '/', namelen));
    return is_dot(name, namelen) || is_dotdot(name, namelen);
}

static inline bool is_dir(com_vnode_t *node) {
    return E_COM_VNODE_TYPE_DIR == node->type;
}

// SAFETY: node is referenced, so vfs->mountpoint is stable (see UNMOUNT)
static com_vnode_t *covered_mountpoint(com_vnode_t *node) {
    if (!node->isroot) {
        return NULL;
    }

    return node->vfs->mountpoint;
}

// Replaces *node with the root mounted on it, if any. Roots cannot be mount
// points, so one step is enough
static void cross_mounts(com_vnode_t **node) {
    com_vnode_t *mountpoint = *node;
    if (NULL == COM_FS_VFS_MOUNTED(mountpoint)) {
        return;
    }

    com_vnode_t *root = NULL;
    krwlock_acquire_read(&mountpoint->lock);
    if (NULL != mountpoint->mountpointof) {
        root = mountpoint->mountpointof->root;
        COM_FS_VFS_VNODE_HOLD(root);
    }
    krwlock_release_read(&mountpoint->lock);

    if (NULL != root) {
        COM_FS_VFS_VNODE_RELEASE(mountpoint);
        *node = root;
    }
}

// LOCKING: dir->lock held in either mode
static int lookup_nolock(com_vnode_t **out,
                         com_vnode_t  *dir,
                         const char   *name,
                         size_t        namelen) {
    *out = NULL;
    if (dir->dead) {
        return ENOENT;
    }

    if (NULL == dir->ops->lookup) {
        return ENOSYS;
    }

    int ret = dir->ops->lookup(out, dir, name, namelen);
    KASSERT(0 != ret || NULL == dir->vfs->cache || (*out)->cached);
    return ret;
}

static int dir_lookup(com_vnode_t **out,
                      com_vnode_t  *dir,
                      const char   *name,
                      size_t        namelen) {
    *out = NULL;
    if (!is_dir(dir)) {
        return ENOTDIR;
    }

    krwlock_acquire_read(&dir->lock);
    int ret = lookup_nolock(out, dir, name, namelen);
    krwlock_release_read(&dir->lock);
    return ret;
}

// LIFETIME

void com_fs_vfs_vnode_init(com_vnode_t     *vnode,
                           com_vfs_t       *vfs,
                           com_vnode_type_t type,
                           com_vnode_ops_t *ops,
                           void            *extra) {
    vnode->ops          = ops;
    vnode->vfs          = vfs;
    vnode->type         = type;
    vnode->ino          = com_fs_vfs_new_ino();
    vnode->num_ref      = 1;
    vnode->extra        = extra;
    vnode->mountpointof = NULL;
    vnode->isroot       = false;
    vnode->cached       = false;
    vnode->dead         = false;
    vnode->binding      = NULL;
    KRWLOCK_INIT(&vnode->lock);

    if (NULL != vfs) {
        __atomic_add_fetch(&vfs->num_ext, 1, __ATOMIC_RELAXED);
    }
}

void com_fs_vfs_inactive(com_vnode_t *vnode) {
    KASSERT(0 == vnode->num_ref);
    KASSERT(NULL == vnode->binding);

    if (NULL != vnode->ops->inactive) {
        vnode->ops->inactive(vnode);
    }
}

// VNODE CACHE

#define CACHE_BUCKETS (ARCH_PAGE_SIZE / sizeof(struct khashmap_entry_tailq))

// LOCKING: lock protects every field and the lru_link of parked vnodes. Its
// mutex may be held while allocating
struct com_vfs_cache {
    ksync_t        lock;
    com_waitlist_t waiters;
    khashmap_t     vnodes; // ino -> vnode, or Busy while loaded or reclaimed
    TAILQ_HEAD(, com_vnode) lru;
    size_t num_parked;
    size_t max_parked;
};

// NOTE: khashmap uses NULL for missing elements, so we use a dummy pointer to
// mark inodes whose vnode is currently busy. Lookups retry when they hit this
static char Busy;

int com_fs_vfs_cache_init(com_vfs_t *vfs, size_t max_parked) {
    struct com_vfs_cache *cache = com_mm_slab_alloc(sizeof(*cache));
    if (NULL == cache) {
        return ENOMEM;
    }

    int ret = khashmap_init(&cache->vnodes, CACHE_BUCKETS);
    if (0 != ret) {
        com_mm_slab_free(cache, sizeof(*cache));
        return ret;
    }

    KSYNC_INIT_MUTEX(&cache->lock);
    COM_SYS_THREAD_WAITLIST_INIT(&cache->waiters);
    TAILQ_INIT(&cache->lru);
    cache->num_parked = 0;
    cache->max_parked = max_parked;
    vfs->cache        = cache;
    return 0;
}

static void cache_reclaim(struct com_vfs_cache *cache, com_vnode_t *node) {
    ino_t ino = node->ino;
    com_fs_vfs_inactive(node);

    // The mapping was previously set to &Busy
    ksync_acquire(&cache->lock);
    KASSERT_CALL(0, ==, khashmap_remove(&cache->vnodes, &ino, sizeof(ino)));
    ksync_notify_all(&cache->lock, &cache->waiters);
    ksync_release(&cache->lock);
}

void com_fs_vfs_cache_destroy(com_vfs_t *vfs) {
    struct com_vfs_cache *cache = vfs->cache;
    com_vnode_t          *node;
    while (NULL != (node = TAILQ_FIRST(&cache->lru))) {
        TAILQ_REMOVE(&cache->lru, node, lru_link);
        KASSERT_CALL(
            0,
            ==,
            khashmap_remove(&cache->vnodes, &node->ino, sizeof(ino_t)));
        com_fs_vfs_inactive(node);
    }

    KASSERT(0 == cache->vnodes.num_entries);
    khashmap_destroy(&cache->vnodes);
    com_mm_slab_free(cache, sizeof(*cache));
    vfs->cache = NULL;
}

int com_fs_vfs_vget(com_vnode_t **out, com_vfs_t *vfs, ino_t ino) {
    struct com_vfs_cache *cache = vfs->cache;
    com_vnode_t          *node  = NULL;
    *out                        = NULL;

    ksync_acquire(&cache->lock);
    for (;;) {
        node = NULL;
        khashmap_get((void **)&node, &cache->vnodes, &ino, sizeof(ino));
        if ((void *)&Busy != node) {
            break;
        }
        ksync_wait_uninterruptible(&cache->lock, &cache->waiters);
    }

    if (NULL != node) {
        // Parked vnodes have no references and are revived here. Active ones
        // may be released concurrently, but never below 1 because that path
        // takes the vnode cache lock, which we hold
        if (0 == __atomic_load_n(&node->num_ref, __ATOMIC_RELAXED)) {
            TAILQ_REMOVE(&cache->lru, node, lru_link);
            cache->num_parked--;
        }
        COM_FS_VFS_VNODE_HOLD(node);
        ksync_release(&cache->lock);
        *out = node;
        return 0;
    }

    // Busy makes other vgets of ino wait while it is loaded without the lock
    int ret = khashmap_put(&cache->vnodes, &ino, sizeof(ino), &Busy);
    ksync_release(&cache->lock);
    if (0 != ret) {
        return ret;
    }

    ret = vfs->type->vget(&node, vfs, ino);
    ksync_acquire(&cache->lock);
    if (0 == ret) {
        node->ino    = ino;
        node->cached = true;
        khashmap_set(&cache->vnodes, &ino, sizeof(ino), node);
        *out = node;
    } else {
        KASSERT_CALL(0, ==, khashmap_remove(&cache->vnodes, &ino, sizeof(ino)));
    }
    ksync_notify_all(&cache->lock, &cache->waiters);
    ksync_release(&cache->lock);
    return ret;
}

void com_fs_vfs_cache_release(com_vnode_t *node) {
    // First we try a lockless pass. We use CAS instead of a simple drop because
    // we don't want to perform the 1 -> 0 drop here
    uintmax_t num_ref = __atomic_load_n(&node->num_ref, __ATOMIC_RELAXED);
    while (num_ref > 1) {
        if (__atomic_compare_exchange_n(&node->num_ref,
                                        &num_ref,
                                        num_ref - 1,
                                        false,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_RELAXED)) {
            return;
        }
    }

    // Here the fast path has failed, so this has a high chance of being a 1 ->
    // 0 drop, and we take the lock for that

    struct com_vfs_cache *cache = node->vfs->cache;
    com_vnode_t          *dead  = NULL;
    ksync_acquire(&cache->lock);
    // High chance != certainty, someone might have grabbed a reference right
    // after our relaxed check
    if (0 != __atomic_add_fetch(&node->num_ref, -1, __ATOMIC_ACQ_REL)) {
        ksync_release(&cache->lock);
        return;
    }

    if (node->vfs->type->keep(node)) {
        TAILQ_INSERT_TAIL(&cache->lru, node, lru_link);
        cache->num_parked++;
        if (cache->num_parked > cache->max_parked) {
            dead = TAILQ_FIRST(&cache->lru);
            TAILQ_REMOVE(&cache->lru, dead, lru_link);
            cache->num_parked--;
        }
    } else {
        dead = node;
    }

    if (NULL != dead) {
        khashmap_set(&cache->vnodes, &dead->ino, sizeof(ino_t), &Busy);
    }
    ksync_release(&cache->lock);

    if (NULL != dead) {
        cache_reclaim(cache, dead);
    }
}

int com_fs_vfs_open(com_vnode_t **out, com_vnode_t *node) {
    if (NULL == node->ops->open) {
        COM_FS_VFS_VNODE_HOLD(node);
        *out = node;
        return 0;
    }

    *out = NULL;
    return node->ops->open(out, node);
}

int com_fs_vfs_close(com_vnode_t *node) {
    if (NULL == node->ops->close) {
        return 0;
    }

    return node->ops->close(node);
}

// MOUNTS

int com_fs_vfs_register_type(com_fs_type_t *type) {
    int ret = 0;
    kspinlock_acquire(&TypesLock);
    com_fs_type_t *curr;
    TAILQ_FOREACH(curr, &Types, types_link) {
        if (0 == kstrcmp(curr->name, type->name)) {
            ret = EEXIST;
            break;
        }
    }
    if (0 == ret) {
        TAILQ_INSERT_TAIL(&Types, type, types_link);
    }
    kspinlock_release(&TypesLock);
    return ret;
}

com_fs_type_t *com_fs_vfs_find_type(const char *name, size_t namelen) {
    com_fs_type_t *curr;
    kspinlock_acquire(&TypesLock);
    TAILQ_FOREACH(curr, &Types, types_link) {
        if (namelen == kstrlen(curr->name) &&
            0 == kmemcmp(curr->name, name, namelen)) {
            break;
        }
    }
    kspinlock_release(&TypesLock);
    return curr;
}

void com_fs_vfs_init_vfs(com_vfs_t *vfs, com_fs_type_t *type, void *extra) {
    static dev_t next_dev = 1;

    vfs->type       = type;
    vfs->mountpoint = NULL;
    vfs->root       = NULL;
    vfs->backing    = NULL;
    vfs->extra      = extra;
    vfs->dev        = __atomic_fetch_add(&next_dev, 1, __ATOMIC_RELAXED);
    vfs->flags      = 0;
    vfs->num_ext    = 0;
    vfs->cache      = NULL;
    KMUTEX_INIT(&vfs->rename_lock);
}

void com_fs_vfs_set_root(com_vfs_t *vfs, com_vnode_t *root) {
    KASSERT(is_dir(root) && root->vfs == vfs && NULL == vfs->root);
    KASSERT(NULL == vfs->cache || root->cached);
    vfs->root    = root;
    root->isroot = true;
}

static int open_backing(com_vnode_t  **backing,
                        com_fs_type_t *type,
                        com_vnode_t   *source,
                        uintmax_t      flags) {
    *backing = NULL;
    if (NULL == source) {
        return EINVAL;
    }

    // Only files and block devices provide storage; IPC bindings hold IPC
    // objects.
    if (E_COM_VNODE_TYPE_BLOCKDEV != source->type &&
        E_COM_VNODE_TYPE_FILE != source->type) {
        return ENOTBLK;
    }

    if (!(COM_FS_VFS_MOUNT_RDONLY & flags) && com_fs_vfs_readonly(source)) {
        return EROFS;
    }

    com_vnode_t *node = NULL;
    int          ret  = com_fs_vfs_open(&node, source);
    if (0 != ret) {
        return ret;
    }

    if (E_COM_VNODE_TYPE_BLOCKDEV != node->type &&
        E_COM_VNODE_TYPE_FILE != node->type) {
        com_fs_vfs_close(node);
        COM_FS_VFS_VNODE_RELEASE(node);
        return ENOTBLK;
    }

    if (0 != com_fs_vfs_bind(node, type)) {
        com_fs_vfs_close(node);
        COM_FS_VFS_VNODE_RELEASE(node);
        return EBUSY;
    }

    *backing = node;
    return 0;
}

static void close_backing(com_vnode_t *backing, com_fs_type_t *type) {
    if (NULL != backing) {
        com_fs_vfs_unbind(backing, type);
        com_fs_vfs_close(backing);
        COM_FS_VFS_VNODE_RELEASE(backing);
    }
}

// LOCKING: mountpoint->lock held in write mode, if not NULL
static int mount_nolock(com_vfs_t    **out,
                        com_fs_type_t *type,
                        com_vnode_t   *backing,
                        com_vnode_t   *mountpoint,
                        uintmax_t      flags) {
    if (NULL != mountpoint) {
        if (mountpoint->dead) {
            return ENOENT;
        }

        if (NULL != mountpoint->mountpointof || mountpoint->isroot) {
            return EBUSY;
        }
    }

    com_vfs_t *vfs = NULL;
    int        ret = type->mount(&vfs, backing, flags);
    if (0 != ret) {
        return ret;
    }

    KASSERT(NULL != vfs->root && type == vfs->type);
    vfs->backing = backing;
    vfs->flags   = flags;

    if (NULL != mountpoint) {
        COM_FS_VFS_VNODE_HOLD(mountpoint);
        vfs->mountpoint = mountpoint;
        __atomic_store_n(&mountpoint->mountpointof, vfs, __ATOMIC_RELAXED);
    }

    *out = vfs;
    return 0;
}

int com_fs_vfs_mount(com_vfs_t    **out,
                     com_fs_type_t *type,
                     com_vnode_t   *source,
                     com_vnode_t   *mountpoint,
                     uintmax_t      flags) {
    *out = NULL;
    if (0 != (flags & ~(uintmax_t)COM_FS_VFS_MOUNT_RDONLY)) {
        return EINVAL;
    }

    if (NULL != mountpoint && !is_dir(mountpoint)) {
        return ENOTDIR;
    }

    com_vnode_t *backing = NULL;
    if (type->needs_backing) {
        int ret = open_backing(&backing, type, source, flags);
        if (0 != ret) {
            return ret;
        }
    }

    // Locked while the file system is created, so that the checks still hold
    if (NULL != mountpoint) {
        krwlock_acquire_write(&mountpoint->lock);
    }
    int ret = mount_nolock(out, type, backing, mountpoint, flags);
    if (NULL != mountpoint) {
        krwlock_release_write(&mountpoint->lock);
    }

    if (0 != ret) {
        close_backing(backing, type);
    }
    return ret;
}

// See UNMOUNT. Pairs with the release in com_fs_vfs_vnode_release, so every
// vnode that was released is fully inactive
static inline bool vfs_is_unused(com_vfs_t *vfs) {
    return 2 == __atomic_load_n(&vfs->num_ext, __ATOMIC_ACQUIRE);
}

int com_fs_vfs_unmount(com_vnode_t *root) {
    com_vfs_t   *vfs        = root->vfs;
    com_vnode_t *mountpoint = root->isroot ? vfs->mountpoint : NULL;
    if (NULL == mountpoint) {
        COM_FS_VFS_VNODE_RELEASE(root);
        return EINVAL;
    }

    com_fs_type_t *type    = vfs->type;
    com_vnode_t   *backing = vfs->backing;
    int            ret     = EBUSY;
    krwlock_acquire_write(&mountpoint->lock);
    KASSERT(vfs == mountpoint->mountpointof);

    if (vfs_is_unused(vfs)) {
        __atomic_store_n(&mountpoint->mountpointof, NULL, __ATOMIC_RELAXED);
        ret = type->unmount(vfs);
        if (0 != ret) {
            __atomic_store_n(&mountpoint->mountpointof, vfs, __ATOMIC_RELAXED);
        }
    }

    krwlock_release_write(&mountpoint->lock);
    if (0 != ret) {
        COM_FS_VFS_VNODE_RELEASE(root);
        return ret;
    }

    // The reference that vfs owned is the caller's now
    close_backing(backing, type);
    COM_FS_VFS_VNODE_RELEASE(mountpoint);
    return 0;
}

// PATHS
//
// The walker resolves buf[pos, end) starting from curr, which it holds. A
// symbolic link replaces what was resolved before it with its contents, so the
// part of the path after it never changes

struct walk {
    char         buf[CONFIG_PATH_MAX];
    size_t       pos;
    size_t       end;
    size_t       num_links;
    com_vnode_t *root;
    com_vnode_t *curr;
};

static int walk_init(struct walk *w,
                     const char  *path,
                     size_t       pathlen,
                     com_vnode_t *root,
                     com_vnode_t *cwd) {
    if (0 == pathlen) {
        return ENOENT;
    }

    if (pathlen >= sizeof(w->buf)) {
        return ENAMETOOLONG;
    }

    com_vnode_t *start = '/' == path[0] ? root : cwd;
    if (NULL == start) {
        return EINVAL;
    }

    kmemcpy(w->buf, path, pathlen);
    w->pos       = 0;
    w->end       = pathlen;
    w->num_links = 0;
    w->root      = root;
    w->curr      = start;
    COM_FS_VFS_VNODE_HOLD(start);
    return 0;
}

static void walk_destroy(struct walk *w) {
    COM_FS_VFS_VNODE_RELEASE(w->curr);
    w->curr = NULL;
}

// The process root is its own parent, and ".." from a mounted root goes to the
// parent of its mount point
static int walk_dotdot(struct walk *w) {
    for (;;) {
        if (w->curr == w->root) {
            return 0;
        }

        com_vnode_t *mountpoint = covered_mountpoint(w->curr);
        if (NULL == mountpoint) {
            break;
        }

        // NOTE: something like /tmp/.. still resolves to / because of this
        // mountpoint check here. w->curr becomes mountpoint, which is a
        // directory in the parent filesystem (e.g., /) so the later isroot
        // check doesn't fire
        COM_FS_VFS_VNODE_HOLD(mountpoint);
        COM_FS_VFS_VNODE_RELEASE(w->curr);
        w->curr = mountpoint;
    }

    // The root of a file system that is not mounted anywhere is its own parent
    if (w->curr->isroot) {
        return 0;
    }

    com_vnode_t *parent = NULL;
    int          ret    = dir_lookup(&parent, w->curr, "..", 2);
    if (0 != ret) {
        return ret;
    }

    COM_FS_VFS_VNODE_RELEASE(w->curr);
    w->curr = parent;
    return 0;
}

// NOTE: ".." may move curr
static int walk_component(com_vnode_t **next,
                          struct walk  *w,
                          const char   *name,
                          size_t        namelen) {
    *next = NULL;
    if (!is_dir(w->curr)) {
        return ENOTDIR;
    }

    if (is_dot(name, namelen)) {
        COM_FS_VFS_VNODE_HOLD(w->curr);
        *next = w->curr;
        return 0;
    }

    if (is_dotdot(name, namelen)) {
        int ret = walk_dotdot(w);
        if (0 == ret) {
            COM_FS_VFS_VNODE_HOLD(w->curr);
            *next = w->curr;
        }
        return ret;
    }

    if (namelen > COM_FS_VFS_NAME_MAX) {
        return ENAMETOOLONG;
    }

    int ret = dir_lookup(next, w->curr, name, namelen);
    if (0 == ret) {
        cross_mounts(next);
    }
    return ret;
}

// Replaces buf[0, rest) with the contents of link. curr is the directory that
// contains link, so the prefix before it is not needed anymore
static int walk_splice(struct walk *w, com_vnode_t *link, size_t rest) {
    if (++w->num_links > CONFIG_SYMLINK_MAX) {
        return ELOOP;
    }

    // The idea is basically to continuously move the "real" path contents
    // toward the end of the buffer. On entry, the path layout is as follows: 0
    // [                                    ] PATH_MAX
    //       ^symlink start   /^rest start
    // Where each component may of course have a length or padding of 0 bytes.
    // So we simply copy the "rest" to the end of the buffer, and replace the
    // symlink section with the name it actually resolves to, then readjust the
    // rest to fit right after the new name and update the relevant start/end
    // cursors

    size_t restlen  = w->end - rest;
    size_t restbase = sizeof(w->buf) - restlen;
    kmemmove(&w->buf[restbase], &w->buf[rest], restlen);

    // We use restbase as the length value for the readlink call in order to
    // guarantee that the read will not overflow onto the "rest" and will
    // therefore fit between 0 and the rest base
    size_t len = 0;
    int    ret = com_fs_vfs_readlink(&len, w->buf, restbase, link);
    if (0 != ret) {
        return ret;
    }

    if (0 == len) {
        return ENOENT;
    }

    // >= so that the result still fits with a NUL
    if (len >= restbase) {
        return ENAMETOOLONG;
    }

    kmemmove(&w->buf[len], &w->buf[restbase], restlen);
    w->pos = 0;
    w->end = len + restlen;

    // If the first character of the buffer (now fully moved to the left) is
    // '/', we need to snap back to the root and start resolving from there
    if ('/' == w->buf[0]) {
        COM_FS_VFS_VNODE_RELEASE(w->curr);
        w->curr = w->root;
        COM_FS_VFS_VNODE_HOLD(w->curr);
    }

    return 0;
}

// Resolves every component but the last one, which is buf[*name, +*namelen)
// NOTE: *namelen == 0 if only slashes are left, and then curr may not be a
// directory
static int
walk_parent(size_t *name, size_t *namelen, bool *must_dir, struct walk *w) {
    for (;;) {
        while (w->pos < w->end && '/' == w->buf[w->pos]) {
            w->pos++;
        }

        *name     = w->pos;
        *namelen  = 0;
        *must_dir = false;
        if (w->pos == w->end) {
            return 0;
        }

        size_t cend = w->pos;
        while (cend < w->end && '/' != w->buf[cend]) {
            cend++;
        }

        size_t after = cend;
        while (after < w->end && '/' == w->buf[after]) {
            after++;
        }

        // cend = end index of the current name
        // after = start index of the next name (starts at cend and increments
        // as long as slashes are encountered, the last increment leads it to
        // point after the last slash)
        // So after == w->end <==> there's nothing past the last slash or
        // there's no trailing slash. Additionally, cend != after <==> there's
        // at least one trailing slash <==> the path must point to a directory

        if (after == w->end) {
            *namelen  = cend - w->pos;
            *must_dir = cend != w->end;
            return is_dir(w->curr) ? 0 : ENOTDIR;
        }

        com_vnode_t *next = NULL;
        int ret = walk_component(&next, w, &w->buf[w->pos], cend - w->pos);
        if (0 != ret) {
            return ret;
        }

        if (E_COM_VNODE_TYPE_LINK == next->type) {
            ret = walk_splice(w, next, cend);
            COM_FS_VFS_VNODE_RELEASE(next);
            if (0 != ret) {
                return ret;
            }
            continue;
        }

        COM_FS_VFS_VNODE_RELEASE(w->curr);
        w->curr = next;
        w->pos  = cend;
    }
}

static int walk_all(com_vnode_t **out, struct walk *w, int flags) {
    *out = NULL;

    for (;;) {
        size_t name, namelen;
        bool   must_dir;
        int    ret = walk_parent(&name, &namelen, &must_dir, w);
        if (0 != ret) {
            return ret;
        }

        if (0 == namelen) {
            COM_FS_VFS_VNODE_HOLD(w->curr);
            *out = w->curr;
            return 0;
        }

        com_vnode_t *next = NULL;
        ret               = walk_component(&next, w, &w->buf[name], namelen);
        if (0 != ret) {
            return ret;
        }

        if (E_COM_VNODE_TYPE_LINK == next->type &&
            ((COM_FS_VFS_LOOKUP_FOLLOW & flags) || must_dir)) {
            ret = walk_splice(w, next, name + namelen);
            COM_FS_VFS_VNODE_RELEASE(next);
            if (0 != ret) {
                return ret;
            }
            continue;
        }

        if (must_dir && !is_dir(next)) {
            COM_FS_VFS_VNODE_RELEASE(next);
            return ENOTDIR;
        }

        *out = next;
        return 0;
    }
}

int com_fs_vfs_lookup(com_vnode_t **out,
                      const char   *path,
                      size_t        pathlen,
                      com_vnode_t  *root,
                      com_vnode_t  *cwd,
                      int           flags) {
    *out = NULL;

    struct walk w;
    int         ret = walk_init(&w, path, pathlen, root, cwd);
    if (0 != ret) {
        return ret;
    }

    ret = walk_all(out, &w, flags);
    walk_destroy(&w);
    return ret;
}

int com_fs_vfs_lookup_parent(com_vnode_t **out,
                             const char  **name,
                             size_t       *namelen,
                             bool         *must_dir,
                             const char   *path,
                             size_t        pathlen,
                             com_vnode_t  *root,
                             com_vnode_t  *cwd) {
    *out = NULL;

    struct walk w;
    int         ret = walk_init(&w, path, pathlen, root, cwd);
    if (0 != ret) {
        return ret;
    }

    size_t wname, wnamelen;
    ret = walk_parent(&wname, &wnamelen, must_dir, &w);
    if (0 != ret) {
        walk_destroy(&w);
        return ret;
    }

    // Splices never touch what follows a link, so the last component is at the
    // same distance from the end of path as from the end of w.buf
    *name    = &path[pathlen - (w.end - wname)];
    *namelen = wnamelen;
    *out     = w.curr;
    return 0;
}

// LOCKING: dir->lock held in write mode
// post: \result == EEXIST ==> *existing is what name links to
static int create_nolock(com_vnode_t           **out,
                         com_vnode_t           **existing,
                         com_vnode_t            *dir,
                         const char             *name,
                         size_t                  namelen,
                         const com_vnode_attr_t *attr) {
    *out = NULL;
    if (dir->dead) {
        return ENOENT;
    }

    com_vnode_t *found = NULL;
    int          ret   = lookup_nolock(&found, dir, name, namelen);
    if (0 == ret) {
        *existing = found;
        return EEXIST;
    }

    if (ENOENT != ret) {
        return ret;
    }

    if (com_fs_vfs_readonly(dir)) {
        return EROFS;
    }

    ret = dir->ops->create(out, dir, name, namelen, attr);
    KASSERT(0 != ret || NULL == dir->vfs->cache || (*out)->cached);
    return ret;
}

static int check_create(com_vnode_t            *dir,
                        const char             *name,
                        size_t                  namelen,
                        const com_vnode_attr_t *attr) {
    if (!is_dir(dir)) {
        return ENOTDIR;
    }

    if (is_dot_name(name, namelen)) {
        return EEXIST;
    }

    if (namelen > COM_FS_VFS_NAME_MAX) {
        return ENAMETOOLONG;
    }

    if (E_COM_VNODE_TYPE_LINK == attr->type) {
        if (0 == attr->targetlen) {
            return ENOENT;
        }

        if (attr->targetlen >= CONFIG_PATH_MAX) {
            return ENAMETOOLONG;
        }
    }

    if (NULL == dir->ops->create) {
        return ENOSYS;
    }

    return 0;
}

int com_fs_vfs_create_path(com_vnode_t           **out,
                           const char             *path,
                           size_t                  pathlen,
                           com_vnode_t            *root,
                           com_vnode_t            *cwd,
                           const com_vnode_attr_t *attr,
                           int                     flags) {
    *out = NULL;

    struct walk w;
    int         ret = walk_init(&w, path, pathlen, root, cwd);
    if (0 != ret) {
        return ret;
    }

    com_vnode_t *existing = NULL;
    bool         must_dir = false;

    for (;;) {
        size_t name, namelen;
        ret = walk_parent(&name, &namelen, &must_dir, &w);
        if (0 != ret) {
            goto end;
        }

        // It makes no sense to "create" the . and .. files
        const char *namep = &w.buf[name];
        if (is_dot_name(namep, namelen)) {
            ret = walk_component(&existing, &w, namep, namelen);
            if (0 != ret) {
                goto end;
            }
            break;
        }

        ret = check_create(w.curr, namep, namelen, attr);
        if (0 != ret) {
            goto end;
        }

        krwlock_acquire_write(&w.curr->lock);
        if (!must_dir || E_COM_VNODE_TYPE_DIR == attr->type) {
            ret = create_nolock(out, &existing, w.curr, namep, namelen, attr);
        } else if (w.curr->dead) {
            ret = ENOENT;
        } else {
            // A name followed by '/' can only be created as a directory
            ret = lookup_nolock(&existing, w.curr, namep, namelen);
            if (0 == ret) {
                ret = EEXIST;
            } else if (ENOENT == ret) {
                ret = E_COM_VNODE_TYPE_FILE == attr->type ? EISDIR : ENOENT;
            }
        }
        krwlock_release_write(&w.curr->lock);

        if (EEXIST != ret || NULL == existing) {
            goto end;
        }

        cross_mounts(&existing);
        if (E_COM_VNODE_TYPE_LINK == existing->type &&
            (COM_FS_VFS_LOOKUP_FOLLOW & flags) &&
            !(COM_FS_VFS_CREATE_EXCL & flags)) {
            ret = walk_splice(&w, existing, name + namelen);
            COM_FS_VFS_VNODE_RELEASE(existing);
            existing = NULL;
            if (0 != ret) {
                goto end;
            }
            continue;
        }

        break;
    }

    // Here we know that the name exists, we only have to choose what to do with
    // it

    if (COM_FS_VFS_CREATE_EXCL & flags) {
        ret = EEXIST;
    } else if (must_dir && !is_dir(existing)) {
        ret = ENOTDIR;
    } else if (E_COM_VNODE_TYPE_FILE == attr->type && is_dir(existing)) {
        ret = EISDIR;
    } else {
        ret      = 0;
        *out     = existing;
        existing = NULL;
    }

end:
    COM_FS_VFS_VNODE_RELEASE(existing);
    walk_destroy(&w);
    return ret;
}

// Prepends "/" and the name of child in parent to buf[*pos, ...)
static int
prepend_name(size_t *pos, char *buf, com_vnode_t *parent, com_vnode_t *child) {
    union {
        com_dirent_t dirent;
        uint8_t      bytes[sizeof(com_dirent_t) + COM_FS_VFS_NAME_MAX + 1];
    } entry;

    uintmax_t off = 0;
    for (;;) {
        size_t    bytes_read = 0;
        uintmax_t next_off   = 0;
        int       ret        = com_fs_vfs_readdir(&entry,
                                                  sizeof(entry),
                                                  &bytes_read,
                                                  &next_off,
                                                  parent,
                                                  off);
        if (0 != ret) {
            return ret;
        }

        if (0 == bytes_read) {
            return ENOENT;
        }

        off = next_off;
        if (entry.dirent.ino != child->ino) {
            continue;
        }

        const char *name    = entry.dirent.name;
        size_t      namelen = kstrlen(name);
        if (is_dot_name(name, namelen)) {
            continue;
        }

        if (namelen + 1 > *pos) {
            return ERANGE;
        }

        *pos -= namelen;
        kmemcpy(&buf[*pos], name, namelen);
        buf[--*pos] = '/';
        return 0;
    }
}

// Very slow and ugly but it gets the job done
int com_fs_vfs_get_path(size_t      *start,
                        char        *buf,
                        size_t       size,
                        com_vnode_t *dir,
                        com_vnode_t *root) {
    if (0 == size) {
        return ERANGE;
    }

    size_t pos        = size - 1;
    buf[pos]          = '\0';
    int          ret  = 0;
    com_vnode_t *curr = dir;
    COM_FS_VFS_VNODE_HOLD(curr);

    while (curr != root) {
        com_vnode_t *mountpoint = covered_mountpoint(curr);
        if (NULL != mountpoint) {
            COM_FS_VFS_VNODE_HOLD(mountpoint);
            COM_FS_VFS_VNODE_RELEASE(curr);
            curr = mountpoint;
            continue;
        }

        // Not reachable from root
        if (curr->isroot) {
            ret = ENOENT;
            break;
        }

        com_vnode_t *parent = NULL;
        ret                 = dir_lookup(&parent, curr, "..", 2);
        if (0 != ret) {
            break;
        }

        ret = prepend_name(&pos, buf, parent, curr);
        COM_FS_VFS_VNODE_RELEASE(curr);
        curr = parent;
        if (0 != ret) {
            break;
        }
    }

    COM_FS_VFS_VNODE_RELEASE(curr);
    if (0 != ret) {
        return ret;
    }

    // dir is root, so only the terminator is there
    if (size - 1 == pos) {
        if (0 == pos) {
            return ERANGE;
        }
        buf[--pos] = '/';
    }

    *start = pos;
    return 0;
}

// NAMES

int com_fs_vfs_create(com_vnode_t           **out,
                      com_vnode_t            *dir,
                      const char             *name,
                      size_t                  namelen,
                      const com_vnode_attr_t *attr) {
    com_vnode_t *dump;
    if (NULL == out) {
        out = &dump;
    }

    *out    = NULL;
    int ret = check_create(dir, name, namelen, attr);
    if (0 != ret) {
        return ret;
    }

    com_vnode_t *existing = NULL;
    krwlock_acquire_write(&dir->lock);
    ret = create_nolock(out, &existing, dir, name, namelen, attr);
    krwlock_release_write(&dir->lock);

    // See LOCKING
    COM_FS_VFS_VNODE_RELEASE(existing);
    if (&dump == out) {
        COM_FS_VFS_VNODE_RELEASE(dump);
    }
    return ret;
}

int com_fs_vfs_link(com_vnode_t *dir,
                    const char  *name,
                    size_t       namelen,
                    com_vnode_t *node) {
    if (!is_dir(dir)) {
        return ENOTDIR;
    }

    if (is_dot_name(name, namelen)) {
        return EEXIST;
    }

    if (namelen > COM_FS_VFS_NAME_MAX) {
        return ENAMETOOLONG;
    }

    if (is_dir(node)) {
        return EPERM;
    }

    if (com_fs_vfs_readonly(dir)) {
        return EROFS;
    }

    if (dir->vfs != node->vfs) {
        return EXDEV;
    }

    if (NULL == dir->ops->link) {
        return ENOSYS;
    }

    com_vnode_t *found = NULL;
    krwlock_acquire_write(&dir->lock);
    int ret = lookup_nolock(&found, dir, name, namelen);
    if (0 == ret) {
        ret = EEXIST;
    } else if (ENOENT == ret && !dir->dead) {
        ret = dir->ops->link(dir, name, namelen, node);
    }
    krwlock_release_write(&dir->lock);

    // See LOCKING
    COM_FS_VFS_VNODE_RELEASE(found);
    return ret;
}

int com_fs_vfs_unlink(com_vnode_t *dir,
                      const char  *name,
                      size_t       namelen,
                      int          flags) {
    if (!is_dir(dir)) {
        return ENOTDIR;
    }

    bool rmdir = COM_FS_VFS_REMOVE_DIR & flags;
    if (is_dot_name(name, namelen)) {
        if (!rmdir) {
            return EISDIR;
        }
        if (0 == namelen) {
            return EBUSY;
        }
        return 1 == namelen ? EINVAL : ENOTEMPTY;
    }

    if (namelen > COM_FS_VFS_NAME_MAX) {
        return ENAMETOOLONG;
    }

    if (NULL == dir->ops->unlink) {
        return ENOSYS;
    }

    if (com_fs_vfs_readonly(dir)) {
        return EROFS;
    }

    com_vnode_t *node = NULL;
    krwlock_acquire_write(&dir->lock);
    int ret = lookup_nolock(&node, dir, name, namelen);
    if (0 != ret) {
        goto end;
    }

    bool node_is_dir = is_dir(node);
    if (!node_is_dir && (rmdir || (COM_FS_VFS_MUST_DIR & flags))) {
        ret = ENOTDIR;
        goto end;
    }

    if (node_is_dir && !rmdir) {
        ret = EISDIR;
        goto end;
    }

    if (!node_is_dir) {
        ret = dir->ops->unlink(dir, name, namelen, node);
        goto end;
    }

    // See MOUNTS
    krwlock_acquire_write(&node->lock);
    if (NULL != node->mountpointof || node->isroot) {
        ret = EBUSY;
    } else {
        ret        = dir->ops->unlink(dir, name, namelen, node);
        node->dead = 0 == ret;
    }
    krwlock_release_write(&node->lock);

end:
    krwlock_release_write(&dir->lock);
    COM_FS_VFS_VNODE_RELEASE(node);
    return ret;
}

// Returns the child of anc that desc is in (or desc itself), referenced, or
// NULL if anc is not an ancestor of desc
// LOCKING: rename_lock held, so ancestry cannot change
static com_vnode_t *rename_trap(com_vnode_t *anc, com_vnode_t *desc) {
    com_vnode_t *prev = NULL;
    com_vnode_t *curr = desc;
    COM_FS_VFS_VNODE_HOLD(curr);

    while (curr != anc && !curr->isroot) {
        com_vnode_t *parent = NULL;

        // A dead directory, rename fails on it later
        if (0 != dir_lookup(&parent, curr, "..", 2)) {
            break;
        }

        COM_FS_VFS_VNODE_RELEASE(prev);
        prev = curr;
        curr = parent;
    }

    if (curr == anc) {
        COM_FS_VFS_VNODE_RELEASE(curr);
        return prev;
    }

    COM_FS_VFS_VNODE_RELEASE(prev);
    COM_FS_VFS_VNODE_RELEASE(curr);
    return NULL;
}

static int check_rename_name(const char *name, size_t namelen) {
    if (is_dot_name(name, namelen)) {
        return 0 == namelen ? EBUSY : EINVAL;
    }

    if (namelen > COM_FS_VFS_NAME_MAX) {
        return ENAMETOOLONG;
    }

    return 0;
}

static void lock_two(com_vnode_t *first, com_vnode_t *second) {
    krwlock_acquire_write(&first->lock);
    if (NULL != second && second != first) {
        krwlock_acquire_write(&second->lock);
    }
}

static void unlock_two(com_vnode_t *first, com_vnode_t *second) {
    if (NULL != second && second != first) {
        krwlock_release_write(&second->lock);
    }
    krwlock_release_write(&first->lock);
}

int com_fs_vfs_rename(com_vnode_t *srcdir,
                      const char  *srcname,
                      size_t       srcnamelen,
                      com_vnode_t *dstdir,
                      const char  *dstname,
                      size_t       dstnamelen,
                      int          flags) {
    if (!is_dir(srcdir) || !is_dir(dstdir)) {
        return ENOTDIR;
    }

    int ret = check_rename_name(srcname, srcnamelen);
    if (0 == ret) {
        ret = check_rename_name(dstname, dstnamelen);
    }
    if (0 != ret) {
        return ret;
    }

    if (srcdir->vfs != dstdir->vfs) {
        return EXDEV;
    }

    if (NULL == srcdir->ops->rename) {
        return ENOSYS;
    }

    if (com_fs_vfs_readonly(srcdir)) {
        return EROFS;
    }

    // The child of srcdir that dstdir is in, or the other way around
    com_vfs_t   *vfs      = srcdir->vfs;
    bool         cross    = srcdir != dstdir;
    com_vnode_t *src_trap = NULL;
    com_vnode_t *dst_trap = NULL;
    com_vnode_t *first    = srcdir;
    com_vnode_t *second   = dstdir;

    if (cross) {
        kmutex_acquire(&vfs->rename_lock);
        src_trap = rename_trap(srcdir, dstdir);
        if (NULL == src_trap) {
            dst_trap = rename_trap(dstdir, srcdir);
        }

        // See LOCK ORDER
        if (NULL != dst_trap ||
            (NULL == src_trap && (uintptr_t)dstdir < (uintptr_t)srcdir)) {
            first  = dstdir;
            second = srcdir;
        }
    }

    com_vnode_t *src = NULL;
    com_vnode_t *dst = NULL;
    lock_two(first, second);

    if (dstdir->dead) {
        ret = ENOENT;
        goto unlock;
    }

    ret = lookup_nolock(&src, srcdir, srcname, srcnamelen);
    if (0 != ret) {
        goto unlock;
    }

    ret = lookup_nolock(&dst, dstdir, dstname, dstnamelen);
    if (ENOENT == ret) {
        ret = 0;
    } else if (0 != ret) {
        goto unlock;
    }

    bool src_is_dir = is_dir(src);
    if (!src_is_dir && (COM_FS_VFS_MUST_DIR & flags)) {
        ret = ENOTDIR;
        goto unlock;
    }

    if (src == dst) {
        goto unlock;
    }

    if (src == src_trap) {
        ret = EINVAL;
        goto unlock;
    }

    if (NULL != dst && dst == dst_trap) {
        ret = ENOTEMPTY;
        goto unlock;
    }

    if (NULL != dst && src_is_dir != is_dir(dst)) {
        ret = src_is_dir ? ENOTDIR : EISDIR;
        goto unlock;
    }

    // The source is only locked if its ".." changes, so two children are only
    // locked together under rename_lock. The trap checks above make sure
    // neither is an ancestor of the other
    com_vnode_t *child1 = NULL;
    com_vnode_t *child2 = NULL;
    if (src_is_dir) {
        child1 = cross ? src : dst;
        child2 = cross ? dst : NULL;
        if (NULL != child2 && (uintptr_t)child2 < (uintptr_t)child1) {
            child1 = dst;
            child2 = src;
        }
        if (NULL != child1) {
            lock_two(child1, child2);
        }
    }

    // See MOUNTS. A source that is not locked may become a mount point
    // concurrently, which just moves the mount with it
    if (NULL != COM_FS_VFS_MOUNTED(src) || src->isroot ||
        (NULL != dst && (NULL != COM_FS_VFS_MOUNTED(dst) || dst->isroot))) {
        ret = EBUSY;
        goto unlock_children;
    }

    ret = srcdir->ops->rename(srcdir,
                              srcname,
                              srcnamelen,
                              src,
                              dstdir,
                              dstname,
                              dstnamelen,
                              dst);
    if (0 == ret && NULL != dst && src_is_dir) {
        dst->dead = true;
    }

unlock_children:
    if (NULL != child1) {
        unlock_two(child1, child2);
    }

unlock:
    unlock_two(first, second);
    if (cross) {
        kmutex_release(&vfs->rename_lock);
    }

    // See LOCKING
    COM_FS_VFS_VNODE_RELEASE(src);
    COM_FS_VFS_VNODE_RELEASE(dst);
    COM_FS_VFS_VNODE_RELEASE(src_trap);
    COM_FS_VFS_VNODE_RELEASE(dst_trap);
    return ret;
}

int com_fs_vfs_readdir(void        *buf,
                       size_t       buflen,
                       size_t      *bytes_read,
                       uintmax_t   *next_off,
                       com_vnode_t *dir,
                       uintmax_t    off) {
    *bytes_read = 0;
    *next_off   = off;
    if (!is_dir(dir)) {
        return ENOTDIR;
    }

    if (NULL == dir->ops->readdir) {
        return ENOSYS;
    }

    int ret = 0;
    krwlock_acquire_read(&dir->lock);
    if (!dir->dead) {
        ret = dir->ops->readdir(buf, buflen, bytes_read, next_off, dir, off);
    }
    krwlock_release_read(&dir->lock);
    return ret;
}

int com_fs_vfs_readlink(size_t      *len,
                        char        *buf,
                        size_t       buflen,
                        com_vnode_t *link) {
    *len = 0;
    if (E_COM_VNODE_TYPE_LINK != link->type) {
        return EINVAL;
    }

    if (NULL == link->ops->readlink) {
        return ENOSYS;
    }

    return link->ops->readlink(len, buf, buflen, link);
}

// BINDINGS

int com_fs_vfs_bind(com_vnode_t *node, void *binding) {
    KASSERT(!is_dir(node) && E_COM_VNODE_TYPE_LINK != node->type);
    KASSERT(NULL != binding);

    int ret = 0;
    krwlock_acquire_write(&node->lock);
    if (NULL != node->binding) {
        ret = EADDRINUSE;
    } else {
        COM_FS_VFS_VNODE_HOLD(node);
        node->binding = binding;
    }
    krwlock_release_write(&node->lock);
    return ret;
}

void com_fs_vfs_unbind(com_vnode_t *node, void *binding) {
    krwlock_acquire_write(&node->lock);
    KASSERT(binding == node->binding);
    node->binding = NULL;
    krwlock_release_write(&node->lock);
    (void)binding;
    COM_FS_VFS_VNODE_RELEASE(node);
}

void *com_fs_vfs_binding_acquire(com_vnode_t *node) {
    krwlock_acquire_read(&node->lock);
    return node->binding;
}

void com_fs_vfs_binding_release(com_vnode_t *node) {
    krwlock_release_read(&node->lock);
}

// DATA AND ATTRIBUTES

// A node without read falls back to readv, and vice versa
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

    *bytes_read = 0;
    if (NULL == node->ops->read) {
        if (NULL != node->ops->readv) {
            struct iovec iov = {.iov_base = buf, .iov_len = buflen};
            kioviter_t   iter;
            kioviter_init(&iter, &iov, 1);
            return node->ops->readv(&iter, bytes_read, node, off, flags);
        }

        return ENOSYS;
    }

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

    *bytes_written = 0;
    if (com_fs_vfs_readonly(node)) {
        return EROFS;
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

    if (NULL == node->ops->read) {
        return ENOSYS;
    }

    struct iovec *iov;
    int           ret      = 0;
    size_t        tot_read = 0;

    for (size_t iov_off = off; NULL != (iov = kioviter_next(ioviter));) {
        size_t curr_read = 0;
        ret              = node->ops->read(iov->iov_base,
                              iov->iov_len,
                              &curr_read,
                              node,
                              iov_off,
                              flags);
        iov_off += curr_read;
        tot_read += curr_read;
        if (0 != ret || curr_read < iov->iov_len) {
            break;
        }
    }

    *bytes_read = tot_read;
    return 0 != tot_read ? 0 : ret;
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

    *bytes_written = 0;
    if (com_fs_vfs_readonly(node)) {
        return EROFS;
    }

    if (NULL != node->ops->writev) {
        return node->ops->writev(bytes_written, node, ioviter, off, flags);
    }

    if (NULL == node->ops->write) {
        return ENOSYS;
    }

    struct iovec *iov;
    int           ret       = 0;
    size_t        tot_write = 0;

    for (size_t iov_off = off; NULL != (iov = kioviter_next(ioviter));) {
        size_t curr_write = 0;
        ret               = node->ops->write(&curr_write,
                               node,
                               iov->iov_base,
                               iov->iov_len,
                               iov_off,
                               flags);
        iov_off += curr_write;
        tot_write += curr_write;
        if (0 != ret || curr_write < iov->iov_len) {
            break;
        }
    }

    *bytes_written = tot_write;
    return 0 != tot_write ? 0 : ret;
}

int com_fs_vfs_truncate(com_vnode_t *node, size_t size) {
    if (is_dir(node)) {
        return EISDIR;
    }

    if (com_fs_vfs_readonly(node)) {
        return EROFS;
    }

    if (NULL == node->ops->truncate) {
        return EINVAL;
    }

    return node->ops->truncate(node, size);
}

int com_fs_vfs_mmap(void             **out,
                    com_vnode_t       *node,
                    com_vmm_context_t *vmm_context,
                    void              *hint,
                    size_t             size,
                    int                vmm_flags,
                    arch_mmu_flags_t   mmu_flags,
                    uintmax_t          off) {
    if ((COM_MM_VMM_FLAGS_SHARED & vmm_flags) &&
        (ARCH_MMU_FLAGS_WRITE & mmu_flags) && com_fs_vfs_readonly(node)) {
        return EROFS;
    }

    if (NULL == node->ops->mmap) {
        return ENOSYS;
    }

    void *dump;
    if (NULL == out) {
        out = &dump;
    }

    return node->ops
        ->mmap(out, node, vmm_context, hint, size, vmm_flags, mmu_flags, off);
}

int com_fs_vfs_stat(struct stat *out, com_vnode_t *node, bool strict) {
    static const mode_t types[] = {
        [E_COM_VNODE_TYPE_FILE]     = S_IFREG,
        [E_COM_VNODE_TYPE_DIR]      = S_IFDIR,
        [E_COM_VNODE_TYPE_LINK]     = S_IFLNK,
        [E_COM_VNODE_TYPE_CHARDEV]  = S_IFCHR,
        [E_COM_VNODE_TYPE_FIFO]     = S_IFIFO,
        [E_COM_VNODE_TYPE_SOCKET]   = S_IFSOCK,
        [E_COM_VNODE_TYPE_BLOCKDEV] = S_IFBLK,
    };

    kmemset(out, sizeof(struct stat), 0);
    int ret = NULL != node->ops->stat ? node->ops->stat(out, node) : ENOSYS;
    if (ENOSYS == ret && !strict) {
        ret = 0;
    }
    if (0 == ret) {
        out->st_dev  = NULL != node->vfs ? node->vfs->dev : 0;
        out->st_ino  = node->ino;
        out->st_mode = (out->st_mode & ~S_IFMT) | types[node->type];
    }
    return ret;
}

int com_fs_vfs_isatty(com_vnode_t *node) {
    if (NULL == node->ops->isatty) {
        return ENOSYS;
    }

    return node->ops->isatty(node);
}

int com_fs_vfs_seekable(com_vnode_t *node) {
    if (NULL == node->ops->seekable) {
        return 0;
    }

    return node->ops->seekable(node);
}

int com_fs_vfs_ioctl(com_vnode_t *node, uintmax_t op, void *buf) {
    if (NULL == node->ops->ioctl) {
        return ENOSYS;
    }

    return node->ops->ioctl(node, op, buf);
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

// UTILITY FUNCTIONS

bool com_fs_vfs_readonly(com_vnode_t *node) {
    return NULL != node->vfs &&
           (COM_FS_VFS_MOUNT_RDONLY &
            __atomic_load_n(&node->vfs->flags, __ATOMIC_RELAXED)) &&
           (E_COM_VNODE_TYPE_FILE == node->type || is_dir(node) ||
            E_COM_VNODE_TYPE_LINK == node->type);
}

ino_t com_fs_vfs_new_ino(void) {
    static ino_t next = 1;
    return __atomic_fetch_add(&next, 1, __ATOMIC_RELAXED);
}
