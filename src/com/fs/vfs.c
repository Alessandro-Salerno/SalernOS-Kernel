#include <arch/info.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/ds/hashmap.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sync/mutex.h>
#include <kernel/com/sync/rwlock.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <vendor/tailq.h>

static TAILQ_HEAD(, FSType) g_types = TAILQ_HEAD_INITIALIZER(g_types);
static Spinlock g_types_lock = SPINLOCK_NEW();

// An empty name is what is left of a path after its last slash
static inline bool is_dot(const char *name, usize namelen) {
    return namelen == 0 || (namelen == 1 && name[0] == '.');
}

static inline bool is_dotdot(const char *name, usize namelen) {
    return namelen == 2 && memcmp(name, "..", 2) == 0;
}

static inline bool is_dot_name(const char *name, usize namelen) {
    assert(memchr(name, '/', namelen) == NULL);
    return is_dot(name, namelen) || is_dotdot(name, namelen);
}

static inline bool is_dir(Vnode *node) { return node->type == VNODE_TYPE_DIR; }

// SAFETY: node is referenced, so vfs->mountpoint is stable (see UNMOUNT)
static Vnode *covered_mountpoint(Vnode *node) {
    if (!node->isroot) return NULL;

    return node->vfs->mountpoint;
}

// Replaces *node with the root mounted on it, if any. Roots cannot be mount
// points, so one step is enough
static void cross_mounts(Vnode **node) {
    Vnode *mountpoint = *node;
    if (vfs_mounted(mountpoint) == NULL) return;

    Vnode *root = NULL;
    rwlock_acq_read(&mountpoint->lock);
    if (mountpoint->mountpointof != NULL) {
        root = mountpoint->mountpointof->root;
        vfs_vnode_hold(root);
    }
    rwlock_rel_read(&mountpoint->lock);

    if (root != NULL) {
        vfs_vnode_rel(mountpoint);
        *node = root;
    }
}

// LOCKING: dir->lock held in either mode
static int lookup_nolock(Vnode *dir, const char *name, usize namelen,
                         Vnode **out_vnode) {
    *out_vnode = NULL;
    if (dir->dead) return ENOENT;
    if (dir->ops->lookup == NULL) return ENOSYS;

    int ret = dir->ops->lookup(dir, name, namelen, out_vnode);
    assert(ret != 0 || dir->vfs->cache == NULL || (*out_vnode)->cached);
    return ret;
}

static int dir_lookup(Vnode *dir, const char *name, usize namelen,
                      Vnode **out_vnode) {
    *out_vnode = NULL;
    if (!is_dir(dir)) return ENOTDIR;

    rwlock_acq_read(&dir->lock);
    int ret = lookup_nolock(dir, name, namelen, out_vnode);
    rwlock_rel_read(&dir->lock);
    return ret;
}

// LIFETIME

void vfs_vnode_init(Vnode *vnode, Vfs *vfs, VnodeType type, VnodeOps *ops,
                    void *extra) {
    vnode->ops = ops;
    vnode->vfs = vfs;
    vnode->type = type;
    vnode->ino = vfs_new_ino();
    vnode->num_ref = 1;
    vnode->extra = extra;
    vnode->mountpointof = NULL;
    vnode->isroot = false;
    vnode->cached = false;
    vnode->dead = false;
    vnode->binding = NULL;
    rwlock_init(&vnode->lock);

    if (vfs != NULL) __atomic_add_fetch(&vfs->num_ext, 1, __ATOMIC_RELAXED);
}

void vfs_inactive(Vnode *vnode) {
    assert(vnode->num_ref == 0);
    assert(vnode->binding == NULL);

    if (vnode->ops->inactive != NULL) vnode->ops->inactive(vnode);
}

// VNODE CACHE

#define CACHE_BUCKETS (PAGE_SIZE / sizeof(struct HashmapEntryTailQ))

// LOCKING: lock protects every field and the lru_link of parked vnodes. Its
// mutex may be held while allocating
struct VFSCache {
    Mutex lock;
    Waitlist waiters;
    Hashmap vnodes; // ino -> vnode, or Busy while loaded or reclaimed
    TAILQ_HEAD(, Vnode) lru;
    usize num_parked;
    usize max_parked;
};

// NOTE: khashmap uses NULL for missing elements, so we use a dummy pointer to
// mark inodes whose vnode is currently busy. Lookups retry when they hit this
static char g_busy;

int vfs_cache_init(Vfs *vfs, usize max_parked) {
    struct VFSCache *cache = slab_alloc(sizeof(*cache));
    if (cache == NULL) return ENOMEM;

    int ret = hashmap_init(&cache->vnodes, CACHE_BUCKETS);
    if (ret != 0) {
        slab_free(cache, sizeof(*cache));
        return ret;
    }

    mutex_init(&cache->lock);
    waitlist_init(&cache->waiters);
    TAILQ_INIT(&cache->lru);
    cache->num_parked = 0;
    cache->max_parked = max_parked;
    vfs->cache = cache;
    return 0;
}

static void cache_reclaim(struct VFSCache *cache, Vnode *node) {
    ino_t ino = node->ino;
    vfs_inactive(node);

    // The mapping was previously set to &Busy
    mutex_acq(&cache->lock);
    verify(hashmap_remove(&cache->vnodes, &ino, sizeof(ino)) == 0);
    sched_notify_all(&cache->waiters);
    mutex_rel(&cache->lock);
}

void vfs_cache_destroy(Vfs *vfs) {
    struct VFSCache *cache = vfs->cache;
    Vnode *node;
    while ((node = TAILQ_FIRST(&cache->lru)) != NULL) {
        TAILQ_REMOVE(&cache->lru, node, lru_link);
        verify(hashmap_remove(&cache->vnodes, &node->ino, sizeof(ino_t)) == 0);
        vfs_inactive(node);
    }

    assert(cache->vnodes.num_entries == 0);
    hashmap_destroy(&cache->vnodes);
    slab_free(cache, sizeof(*cache));
    vfs->cache = NULL;
}

int vfs_vget(Vfs *vfs, ino_t ino, Vnode **out_vnode) {
    struct VFSCache *cache = vfs->cache;
    Vnode *node = NULL;
    *out_vnode = NULL;

    mutex_acq(&cache->lock);
    for (;;) {
        node = NULL;
        hashmap_get(&cache->vnodes, &ino, sizeof(ino), (void **)&node);
        if ((void *)&g_busy != node) break;
        mutex_wait(&cache->lock, &cache->waiters, 0, false);
    }

    if (node != NULL) {
        // Parked vnodes have no references and are revived here. Active ones
        // may be released concurrently, but never below 1 because that path
        // takes the vnode cache lock, which we hold
        if (__atomic_load_n(&node->num_ref, __ATOMIC_RELAXED) == 0) {
            TAILQ_REMOVE(&cache->lru, node, lru_link);
            cache->num_parked--;
        }
        vfs_vnode_hold(node);
        mutex_rel(&cache->lock);
        *out_vnode = node;
        return 0;
    }

    // Busy makes other vgets of ino wait while it is loaded without the lock
    int ret = hashmap_put(&cache->vnodes, &ino, sizeof(ino), &g_busy);
    mutex_rel(&cache->lock);
    if (ret != 0) return ret;

    ret = vfs->type->vget(vfs, ino, &node);
    mutex_acq(&cache->lock);
    if (ret == 0) {
        node->ino = ino;
        node->cached = true;
        hashmap_set(&cache->vnodes, &ino, sizeof(ino), node);
        *out_vnode = node;
    } else {
        verify(hashmap_remove(&cache->vnodes, &ino, sizeof(ino)) == 0);
    }
    sched_notify_all(&cache->waiters);
    mutex_rel(&cache->lock);
    return ret;
}

void vfs_cache_rel(Vnode *node) {
    // First we try a lockless pass. We use CAS instead of a simple drop because
    // we don't want to perform the 1 -> 0 drop here
    u64 num_ref = __atomic_load_n(&node->num_ref, __ATOMIC_RELAXED);
    while (num_ref > 1) {
        if (__atomic_compare_exchange_n(&node->num_ref, &num_ref, num_ref - 1,
                                        false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_RELAXED)) {
            return;
        }
    }

    // Here the fast path has failed, so this has a high chance of being a 1 ->
    // 0 drop, and we take the lock for that

    struct VFSCache *cache = node->vfs->cache;
    Vnode *dead = NULL;
    mutex_acq(&cache->lock);
    // High chance != certainty, someone might have grabbed a reference right
    // after our relaxed check
    if (__atomic_add_fetch(&node->num_ref, -1, __ATOMIC_ACQ_REL) != 0) {
        mutex_rel(&cache->lock);
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

    if (dead != NULL) {
        hashmap_set(&cache->vnodes, &dead->ino, sizeof(ino_t), &g_busy);
    }
    mutex_rel(&cache->lock);

    if (dead != NULL) cache_reclaim(cache, dead);
}

int vfs_open(Vnode *node, Vnode **out_vnode) {
    if (node->ops->open == NULL) {
        vfs_vnode_hold(node);
        *out_vnode = node;
        return 0;
    }

    *out_vnode = NULL;
    return node->ops->open(node, out_vnode);
}

int vfs_close(Vnode *node) {
    if (node->ops->close == NULL) return 0;

    return node->ops->close(node);
}

// MOUNTS

int vfs_register_type(FSType *type) {
    int ret = 0;
    spinlock_acq(&g_types_lock);
    FSType *curr;
    TAILQ_FOREACH(curr, &g_types, types_link) {
        if (strcmp(curr->name, type->name) == 0) {
            ret = EEXIST;
            break;
        }
    }
    if (ret == 0) TAILQ_INSERT_TAIL(&g_types, type, types_link);
    spinlock_rel(&g_types_lock);
    return ret;
}

FSType *vfs_find_type(const char *name, usize namelen) {
    FSType *curr;
    spinlock_acq(&g_types_lock);
    TAILQ_FOREACH(curr, &g_types, types_link) {
        if (namelen == strlen(curr->name) &&
            memcmp(curr->name, name, namelen) == 0) {
            break;
        }
    }
    spinlock_rel(&g_types_lock);
    return curr;
}

void vfs_init_vfs(Vfs *vfs, FSType *type, void *extra) {
    static dev_t next_dev = 1;

    vfs->type = type;
    vfs->mountpoint = NULL;
    vfs->root = NULL;
    vfs->backing = NULL;
    vfs->extra = extra;
    vfs->dev = __atomic_fetch_add(&next_dev, 1, __ATOMIC_RELAXED);
    vfs->flags = 0;
    vfs->num_ext = 0;
    vfs->cache = NULL;
    mutex_init(&vfs->rename_lock);
}

void vfs_set_root(Vfs *vfs, Vnode *root) {
    assert(is_dir(root) && root->vfs == vfs && vfs->root == NULL);
    assert(vfs->cache == NULL || root->cached);
    vfs->root = root;
    root->isroot = true;
}

static int open_backing(FSType *type, Vnode *source, u64 flags,
                        Vnode **out_backing) {
    *out_backing = NULL;
    if (source == NULL) return EINVAL;

    // Only files and block devices provide storage; IPC bindings hold IPC
    // objects.
    if (source->type != VNODE_TYPE_BLOCKDEV &&
        source->type != VNODE_TYPE_FILE) {
        return ENOTBLK;
    }

    if (!(flags & VFS_MOUNT_RDONLY) && vfs_readonly(source)) return EROFS;

    Vnode *node = NULL;
    int ret = vfs_open(source, &node);
    if (ret != 0) return ret;

    if (node->type != VNODE_TYPE_BLOCKDEV && node->type != VNODE_TYPE_FILE) {
        vfs_close(node);
        vfs_vnode_rel(node);
        return ENOTBLK;
    }

    if (vfs_bind(node, type) != 0) {
        vfs_close(node);
        vfs_vnode_rel(node);
        return EBUSY;
    }

    *out_backing = node;
    return 0;
}

static void close_backing(Vnode *backing, FSType *type) {
    if (backing != NULL) {
        vfs_unbind(backing, type);
        vfs_close(backing);
        vfs_vnode_rel(backing);
    }
}

// LOCKING: mountpoint->lock held in write mode, if not NULL
static int mount_nolock(FSType *type, Vnode *backing, Vnode *mountpoint,
                        u64 flags, Vfs **out_vfs) {
    if (mountpoint != NULL) {
        if (mountpoint->dead) return ENOENT;

        if (mountpoint->mountpointof != NULL || mountpoint->isroot) {
            return EBUSY;
        }
    }

    Vfs *vfs = NULL;
    int ret = type->mount(backing, flags, &vfs);
    if (ret != 0) return ret;

    assert(vfs->root != NULL && type == vfs->type);
    vfs->backing = backing;
    vfs->flags = flags;

    if (mountpoint != NULL) {
        vfs_vnode_hold(mountpoint);
        vfs->mountpoint = mountpoint;
        __atomic_store_n(&mountpoint->mountpointof, vfs, __ATOMIC_RELAXED);
    }

    *out_vfs = vfs;
    return 0;
}

int vfs_mount(FSType *type, Vnode *source, Vnode *mountpoint, u64 flags,
              Vfs **out_vfs) {
    *out_vfs = NULL;
    if ((flags & ~(u64)VFS_MOUNT_RDONLY) != 0) return EINVAL;
    if (mountpoint != NULL && !is_dir(mountpoint)) return ENOTDIR;

    Vnode *backing = NULL;
    if (type->needs_backing) {
        int ret = open_backing(type, source, flags, &backing);
        if (ret != 0) return ret;
    }

    // Locked while the file system is created, so that the checks still hold
    if (mountpoint != NULL) rwlock_acq_write(&mountpoint->lock);
    int ret = mount_nolock(type, backing, mountpoint, flags, out_vfs);
    if (mountpoint != NULL) rwlock_rel_write(&mountpoint->lock);

    if (ret != 0) close_backing(backing, type);
    return ret;
}

// See UNMOUNT. Pairs with the release in vfs_vnode_rel, so every
// vnode that was released is fully inactive
static inline bool vfs_is_unused(Vfs *vfs) {
    return __atomic_load_n(&vfs->num_ext, __ATOMIC_ACQUIRE) == 2;
}

int vfs_unmount(Vnode *root) {
    Vfs *vfs = root->vfs;
    Vnode *mountpoint = root->isroot ? vfs->mountpoint : NULL;
    if (mountpoint == NULL) {
        vfs_vnode_rel(root);
        return EINVAL;
    }

    FSType *type = vfs->type;
    Vnode *backing = vfs->backing;
    int ret = EBUSY;
    rwlock_acq_write(&mountpoint->lock);
    assert(vfs == mountpoint->mountpointof);

    if (vfs_is_unused(vfs)) {
        __atomic_store_n(&mountpoint->mountpointof, NULL, __ATOMIC_RELAXED);
        ret = type->unmount(vfs);
        if (ret != 0) {
            __atomic_store_n(&mountpoint->mountpointof, vfs, __ATOMIC_RELAXED);
        }
    }

    rwlock_rel_write(&mountpoint->lock);
    if (ret != 0) {
        vfs_vnode_rel(root);
        return ret;
    }

    // The reference that vfs owned is the caller's now
    close_backing(backing, type);
    vfs_vnode_rel(mountpoint);
    return 0;
}

// PATHS
//
// The walker resolves buf[pos, end) starting from curr, which it holds. A
// symbolic link replaces what was resolved before it with its contents, so the
// part of the path after it never changes

struct Walk {
    char buf[CONFIG_PATH_MAX];
    usize pos;
    usize end;
    usize num_links;
    Vnode *root;
    Vnode *curr;
};

static int walk_init(struct Walk *w, const char *path, usize pathlen,
                     Vnode *root, Vnode *cwd) {
    if (pathlen == 0) return ENOENT;
    if (pathlen >= sizeof(w->buf)) return ENAMETOOLONG;

    Vnode *start = path[0] == '/' ? root : cwd;
    if (start == NULL) return EINVAL;

    memcpy(w->buf, path, pathlen);
    w->pos = 0;
    w->end = pathlen;
    w->num_links = 0;
    w->root = root;
    w->curr = start;
    vfs_vnode_hold(start);
    return 0;
}

static void walk_destroy(struct Walk *w) {
    vfs_vnode_rel(w->curr);
    w->curr = NULL;
}

// The process root is its own parent, and ".." from a mounted root goes to the
// parent of its mount point
static int walk_dotdot(struct Walk *w) {
    for (;;) {
        if (w->curr == w->root) return 0;

        Vnode *mountpoint = covered_mountpoint(w->curr);
        if (mountpoint == NULL) break;

        // NOTE: something like /tmp/.. still resolves to / because of this
        // mountpoint check here. w->curr becomes mountpoint, which is a
        // directory in the parent filesystem (e.g., /) so the later isroot
        // check doesn't fire
        vfs_vnode_hold(mountpoint);
        vfs_vnode_rel(w->curr);
        w->curr = mountpoint;
    }

    // The root of a file system that is not mounted anywhere is its own parent
    if (w->curr->isroot) return 0;

    Vnode *parent = NULL;
    int ret = dir_lookup(w->curr, "..", 2, &parent);
    if (ret != 0) return ret;

    vfs_vnode_rel(w->curr);
    w->curr = parent;
    return 0;
}

// NOTE: ".." may move curr
static int walk_component(struct Walk *w, const char *name, usize namelen,
                          Vnode **out_next) {
    *out_next = NULL;
    if (!is_dir(w->curr)) return ENOTDIR;

    if (is_dot(name, namelen)) {
        vfs_vnode_hold(w->curr);
        *out_next = w->curr;
        return 0;
    }

    if (is_dotdot(name, namelen)) {
        int ret = walk_dotdot(w);
        if (ret == 0) {
            vfs_vnode_hold(w->curr);
            *out_next = w->curr;
        }
        return ret;
    }

    if (namelen > VFS_NAME_MAX) return ENAMETOOLONG;

    int ret = dir_lookup(w->curr, name, namelen, out_next);
    if (ret == 0) cross_mounts(out_next);
    return ret;
}

// Replaces buf[0, rest) with the contents of link. curr is the directory that
// contains link, so the prefix before it is not needed anymore
static int walk_splice(struct Walk *w, Vnode *link, usize rest) {
    if (++w->num_links > CONFIG_SYMLINK_MAX) return ELOOP;

    // The idea is basically to continuously move the "real" path contents
    // toward the end of the buffer. On entry, the path layout is as follows: 0
    // [                                    ] PATH_MAX
    //       ^symlink start   /^rest start
    // Where each component may of course have a length or padding of 0 bytes.
    // So we simply copy the "rest" to the end of the buffer, and replace the
    // symlink section with the name it actually resolves to, then readjust the
    // rest to fit right after the new name and update the relevant start/end
    // cursors

    usize restlen = w->end - rest;
    usize restbase = sizeof(w->buf) - restlen;
    memmove(&w->buf[restbase], &w->buf[rest], restlen);

    // We use restbase as the length value for the readlink call in order to
    // guarantee that the read will not overflow onto the "rest" and will
    // therefore fit between 0 and the rest base
    usize len = 0;
    int ret = vfs_readlink(w->buf, restbase, link, &len);
    if (ret != 0) return ret;
    if (len == 0) return ENOENT;

    // >= so that the result still fits with a NUL
    if (len >= restbase) return ENAMETOOLONG;

    memmove(&w->buf[len], &w->buf[restbase], restlen);
    w->pos = 0;
    w->end = len + restlen;

    // If the first character of the buffer (now fully moved to the left) is
    // '/', we need to snap back to the root and start resolving from there
    if (w->buf[0] == '/') {
        vfs_vnode_rel(w->curr);
        w->curr = w->root;
        vfs_vnode_hold(w->curr);
    }

    return 0;
}

// Resolves every component but the last one, which is buf[*name, +*namelen)
// NOTE: *namelen == 0 if only slashes are left, and then curr may not be a
// directory
static int walk_parent(usize *name, usize *namelen, bool *must_dir,
                       struct Walk *w) {
    for (;;) {
        while (w->pos < w->end && w->buf[w->pos] == '/') w->pos++;

        *name = w->pos;
        *namelen = 0;
        *must_dir = false;
        if (w->pos == w->end) return 0;

        usize cend = w->pos;
        while (cend < w->end && w->buf[cend] != '/') cend++;

        usize after = cend;
        while (after < w->end && w->buf[after] == '/') after++;

        // cend = end index of the current name
        // after = start index of the next name (starts at cend and increments
        // as long as slashes are encountered, the last increment leads it to
        // point after the last slash)
        // So after == w->end <==> there's nothing past the last slash or
        // there's no trailing slash. Additionally, cend != after <==> there's
        // at least one trailing slash <==> the path must point to a directory

        if (after == w->end) {
            *namelen = cend - w->pos;
            *must_dir = cend != w->end;
            return is_dir(w->curr) ? 0 : ENOTDIR;
        }

        Vnode *next = NULL;
        int ret = walk_component(w, &w->buf[w->pos], cend - w->pos, &next);
        if (ret != 0) return ret;

        if (next->type == VNODE_TYPE_LINK) {
            ret = walk_splice(w, next, cend);
            vfs_vnode_rel(next);
            if (ret != 0) return ret;
            continue;
        }

        vfs_vnode_rel(w->curr);
        w->curr = next;
        w->pos = cend;
    }
}

static int walk_all(struct Walk *w, int flags, Vnode **out_vnode) {
    *out_vnode = NULL;

    for (;;) {
        usize name, namelen;
        bool must_dir;
        int ret = walk_parent(&name, &namelen, &must_dir, w);
        if (ret != 0) return ret;

        if (namelen == 0) {
            vfs_vnode_hold(w->curr);
            *out_vnode = w->curr;
            return 0;
        }

        Vnode *next = NULL;
        ret = walk_component(w, &w->buf[name], namelen, &next);
        if (ret != 0) return ret;

        if (next->type == VNODE_TYPE_LINK &&
            ((flags & VFS_LOOKUP_FOLLOW) || must_dir)) {
            ret = walk_splice(w, next, name + namelen);
            vfs_vnode_rel(next);
            if (ret != 0) return ret;
            continue;
        }

        if (must_dir && !is_dir(next)) {
            vfs_vnode_rel(next);
            return ENOTDIR;
        }

        *out_vnode = next;
        return 0;
    }
}

int vfs_lookup(const char *path, usize pathlen, Vnode *root, Vnode *cwd,
               int flags, Vnode **out_vnode) {
    *out_vnode = NULL;

    struct Walk w;
    int ret = walk_init(&w, path, pathlen, root, cwd);
    if (ret != 0) return ret;

    ret = walk_all(&w, flags, out_vnode);
    walk_destroy(&w);
    return ret;
}

int vfs_lookup_parent(const char *path, usize pathlen, Vnode *root, Vnode *cwd,
                      Vnode **out_vnode, const char **out_name,
                      usize *out_namelen, bool *out_must_dir) {
    *out_vnode = NULL;

    struct Walk w;
    int ret = walk_init(&w, path, pathlen, root, cwd);
    if (ret != 0) return ret;

    usize wname, wnamelen;
    ret = walk_parent(&wname, &wnamelen, out_must_dir, &w);
    if (ret != 0) {
        walk_destroy(&w);
        return ret;
    }

    // Splices never touch what follows a link, so the last component is at the
    // same distance from the end of path as from the end of w.buf
    *out_name = &path[pathlen - (w.end - wname)];
    *out_namelen = wnamelen;
    *out_vnode = w.curr;
    return 0;
}

// LOCKING: dir->lock held in write mode
// post: \result == EEXIST ==> *existing is what name links to
static int create_nolock(Vnode **existing, Vnode *dir, const char *name,
                         usize namelen, const VnodeAttr *attr,
                         Vnode **out_vnode) {
    *out_vnode = NULL;
    if (dir->dead) return ENOENT;

    Vnode *found = NULL;
    int ret = lookup_nolock(dir, name, namelen, &found);
    if (ret == 0) {
        *existing = found;
        return EEXIST;
    }

    if (ret != ENOENT) return ret;

    if (vfs_readonly(dir)) return EROFS;

    ret = dir->ops->create(dir, name, namelen, attr, out_vnode);
    assert(ret != 0 || dir->vfs->cache == NULL || (*out_vnode)->cached);
    return ret;
}

static int check_create(Vnode *dir, const char *name, usize namelen,
                        const VnodeAttr *attr) {
    if (!is_dir(dir)) return ENOTDIR;
    if (is_dot_name(name, namelen)) return EEXIST;
    if (namelen > VFS_NAME_MAX) return ENAMETOOLONG;

    if (attr->type == VNODE_TYPE_LINK) {
        if (attr->targetlen == 0) return ENOENT;
        if (attr->targetlen >= CONFIG_PATH_MAX) return ENAMETOOLONG;
    }

    if (dir->ops->create == NULL) return ENOSYS;

    return 0;
}

int vfs_create_path(const char *path, usize pathlen, Vnode *root, Vnode *cwd,
                    const VnodeAttr *attr, int flags, Vnode **out_vnode) {
    *out_vnode = NULL;

    struct Walk w;
    int ret = walk_init(&w, path, pathlen, root, cwd);
    if (ret != 0) return ret;

    Vnode *existing = NULL;
    bool must_dir = false;

    for (;;) {
        usize name, namelen;
        ret = walk_parent(&name, &namelen, &must_dir, &w);
        if (ret != 0) goto end;

        // It makes no sense to "create" the . and .. files
        const char *namep = &w.buf[name];
        if (is_dot_name(namep, namelen)) {
            ret = walk_component(&w, namep, namelen, &existing);
            if (ret != 0) goto end;
            break;
        }

        ret = check_create(w.curr, namep, namelen, attr);
        if (ret != 0) goto end;

        rwlock_acq_write(&w.curr->lock);
        if (!must_dir || attr->type == VNODE_TYPE_DIR) {
            ret = create_nolock(&existing, w.curr, namep, namelen, attr,
                                out_vnode);
        } else if (w.curr->dead) {
            ret = ENOENT;
        } else {
            // A name followed by '/' can only be created as a directory
            ret = lookup_nolock(w.curr, namep, namelen, &existing);
            if (ret == 0) {
                ret = EEXIST;
            } else if (ret == ENOENT) {
                ret = attr->type == VNODE_TYPE_FILE ? EISDIR : ENOENT;
            }
        }
        rwlock_rel_write(&w.curr->lock);

        if (ret != EEXIST || existing == NULL) goto end;

        cross_mounts(&existing);
        if (existing->type == VNODE_TYPE_LINK && (flags & VFS_LOOKUP_FOLLOW) &&
            !(flags & VFS_CREATE_EXCL)) {
            ret = walk_splice(&w, existing, name + namelen);
            vfs_vnode_rel(existing);
            existing = NULL;
            if (ret != 0) goto end;
            continue;
        }

        break;
    }

    // Here we know that the name exists, we only have to choose what to do with
    // it

    if (flags & VFS_CREATE_EXCL) {
        ret = EEXIST;
    } else if (must_dir && !is_dir(existing)) {
        ret = ENOTDIR;
    } else if (attr->type == VNODE_TYPE_FILE && is_dir(existing)) {
        ret = EISDIR;
    } else {
        ret = 0;
        *out_vnode = existing;
        existing = NULL;
    }

end:
    vfs_vnode_rel(existing);
    walk_destroy(&w);
    return ret;
}

// Prepends "/" and the name of child in parent to buf[*pos, ...)
static int prepend_name(usize *pos, char *buf, Vnode *parent, Vnode *child) {
    union {
        VFSDirEnt dirent;
        u8 bytes[sizeof(VFSDirEnt) + VFS_NAME_MAX + 1];
    } entry;

    u64 off = 0;
    for (;;) {
        usize bytes_read = 0;
        u64 next_off = 0;
        int ret = vfs_readdir(&entry, sizeof(entry), parent, off, &bytes_read,
                              &next_off);
        if (ret != 0) return ret;
        if (bytes_read == 0) return ENOENT;

        off = next_off;
        if (entry.dirent.ino != child->ino) continue;

        const char *name = entry.dirent.name;
        usize namelen = strlen(name);
        if (is_dot_name(name, namelen)) continue;
        if (namelen + 1 > *pos) return ERANGE;

        *pos -= namelen;
        memcpy(&buf[*pos], name, namelen);
        buf[--*pos] = '/';
        return 0;
    }
}

// Very slow and ugly but it gets the job done
int vfs_get_path(char *buf, usize size, Vnode *dir, Vnode *root,
                 usize *out_start) {
    if (size == 0) return ERANGE;

    usize pos = size - 1;
    buf[pos] = '\0';
    int ret = 0;
    Vnode *curr = dir;
    vfs_vnode_hold(curr);

    while (curr != root) {
        Vnode *mountpoint = covered_mountpoint(curr);
        if (mountpoint != NULL) {
            vfs_vnode_hold(mountpoint);
            vfs_vnode_rel(curr);
            curr = mountpoint;
            continue;
        }

        // Not reachable from root
        if (curr->isroot) {
            ret = ENOENT;
            break;
        }

        Vnode *parent = NULL;
        ret = dir_lookup(curr, "..", 2, &parent);
        if (ret != 0) break;

        ret = prepend_name(&pos, buf, parent, curr);
        vfs_vnode_rel(curr);
        curr = parent;
        if (ret != 0) break;
    }

    vfs_vnode_rel(curr);
    if (ret != 0) return ret;

    // dir is root, so only the terminator is there
    if (pos == size - 1) {
        if (pos == 0) return ERANGE;
        buf[--pos] = '/';
    }

    *out_start = pos;
    return 0;
}

// NAMES

int vfs_create(Vnode *dir, const char *name, usize namelen,
               const VnodeAttr *attr, Vnode **out_vnode) {
    Vnode *dump;
    if (out_vnode == NULL) out_vnode = &dump;

    *out_vnode = NULL;
    int ret = check_create(dir, name, namelen, attr);
    if (ret != 0) return ret;

    Vnode *existing = NULL;
    rwlock_acq_write(&dir->lock);
    ret = create_nolock(&existing, dir, name, namelen, attr, out_vnode);
    rwlock_rel_write(&dir->lock);

    // See LOCKING
    vfs_vnode_rel(existing);
    if (&dump == out_vnode) vfs_vnode_rel(dump);
    return ret;
}

int vfs_link(Vnode *dir, const char *name, usize namelen, Vnode *node) {
    if (!is_dir(dir)) return ENOTDIR;
    if (is_dot_name(name, namelen)) return EEXIST;
    if (namelen > VFS_NAME_MAX) return ENAMETOOLONG;
    if (is_dir(node)) return EPERM;
    if (vfs_readonly(dir)) return EROFS;
    if (dir->vfs != node->vfs) return EXDEV;
    if (dir->ops->link == NULL) return ENOSYS;

    Vnode *found = NULL;
    rwlock_acq_write(&dir->lock);
    int ret = lookup_nolock(dir, name, namelen, &found);
    if (ret == 0) {
        ret = EEXIST;
    } else if (ret == ENOENT && !dir->dead) {
        ret = dir->ops->link(dir, name, namelen, node);
    }
    rwlock_rel_write(&dir->lock);

    // See LOCKING
    vfs_vnode_rel(found);
    return ret;
}

int vfs_unlink(Vnode *dir, const char *name, usize namelen, int flags) {
    if (!is_dir(dir)) return ENOTDIR;

    bool rmdir = flags & VFS_REMOVE_DIR;
    if (is_dot_name(name, namelen)) {
        if (!rmdir) return EISDIR;
        if (namelen == 0) return EBUSY;
        return namelen == 1 ? EINVAL : ENOTEMPTY;
    }

    if (namelen > VFS_NAME_MAX) return ENAMETOOLONG;
    if (dir->ops->unlink == NULL) return ENOSYS;
    if (vfs_readonly(dir)) return EROFS;

    Vnode *node = NULL;
    rwlock_acq_write(&dir->lock);
    int ret = lookup_nolock(dir, name, namelen, &node);
    if (ret != 0) goto end;

    bool node_is_dir = is_dir(node);
    if (!node_is_dir && (rmdir || (flags & VFS_MUST_DIR))) {
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
    rwlock_acq_write(&node->lock);
    if (node->mountpointof != NULL || node->isroot) {
        ret = EBUSY;
    } else {
        ret = dir->ops->unlink(dir, name, namelen, node);
        node->dead = ret == 0;
    }
    rwlock_rel_write(&node->lock);

end:
    rwlock_rel_write(&dir->lock);
    vfs_vnode_rel(node);
    return ret;
}

// Returns the child of anc that desc is in (or desc itself), referenced, or
// NULL if anc is not an ancestor of desc
// LOCKING: rename_lock held, so ancestry cannot change
static Vnode *rename_trap(Vnode *anc, Vnode *desc) {
    Vnode *prev = NULL;
    Vnode *curr = desc;
    vfs_vnode_hold(curr);

    while (curr != anc && !curr->isroot) {
        Vnode *parent = NULL;

        // A dead directory, rename fails on it later
        if (dir_lookup(curr, "..", 2, &parent) != 0) break;

        vfs_vnode_rel(prev);
        prev = curr;
        curr = parent;
    }

    if (curr == anc) {
        vfs_vnode_rel(curr);
        return prev;
    }

    vfs_vnode_rel(prev);
    vfs_vnode_rel(curr);
    return NULL;
}

static int check_rename_name(const char *name, usize namelen) {
    if (is_dot_name(name, namelen)) return namelen == 0 ? EBUSY : EINVAL;
    if (namelen > VFS_NAME_MAX) return ENAMETOOLONG;

    return 0;
}

static void lock_two(Vnode *first, Vnode *second) {
    rwlock_acq_write(&first->lock);
    if (second != NULL && second != first) rwlock_acq_write(&second->lock);
}

static void unlock_two(Vnode *first, Vnode *second) {
    if (second != NULL && second != first) rwlock_rel_write(&second->lock);
    rwlock_rel_write(&first->lock);
}

int vfs_rename(Vnode *srcdir, const char *srcname, usize srcnamelen,
               Vnode *dstdir, const char *dstname, usize dstnamelen,
               int flags) {
    if (!is_dir(srcdir) || !is_dir(dstdir)) return ENOTDIR;

    int ret = check_rename_name(srcname, srcnamelen);
    if (ret == 0) ret = check_rename_name(dstname, dstnamelen);
    if (ret != 0) return ret;

    if (srcdir->vfs != dstdir->vfs) return EXDEV;
    if (srcdir->ops->rename == NULL) return ENOSYS;
    if (vfs_readonly(srcdir)) return EROFS;

    // The child of srcdir that dstdir is in, or the other way around
    Vfs *vfs = srcdir->vfs;
    bool cross = srcdir != dstdir;
    Vnode *src_trap = NULL;
    Vnode *dst_trap = NULL;
    Vnode *first = srcdir;
    Vnode *second = dstdir;

    if (cross) {
        mutex_acq(&vfs->rename_lock);
        src_trap = rename_trap(srcdir, dstdir);
        if (src_trap == NULL) dst_trap = rename_trap(dstdir, srcdir);

        // See LOCK ORDER
        if (dst_trap != NULL ||
            (src_trap == NULL && (uptr)dstdir < (uptr)srcdir)) {
            first = dstdir;
            second = srcdir;
        }
    }

    Vnode *src = NULL;
    Vnode *dst = NULL;
    lock_two(first, second);

    if (dstdir->dead) {
        ret = ENOENT;
        goto unlock;
    }

    ret = lookup_nolock(srcdir, srcname, srcnamelen, &src);
    if (ret != 0) goto unlock;

    ret = lookup_nolock(dstdir, dstname, dstnamelen, &dst);
    if (ret == ENOENT) ret = 0;
    else if (ret != 0) goto unlock;

    bool src_is_dir = is_dir(src);
    if (!src_is_dir && (flags & VFS_MUST_DIR)) {
        ret = ENOTDIR;
        goto unlock;
    }

    if (src == dst) goto unlock;

    if (src == src_trap) {
        ret = EINVAL;
        goto unlock;
    }

    if (dst != NULL && dst == dst_trap) {
        ret = ENOTEMPTY;
        goto unlock;
    }

    if (dst != NULL && src_is_dir != is_dir(dst)) {
        ret = src_is_dir ? ENOTDIR : EISDIR;
        goto unlock;
    }

    // The source is only locked if its ".." changes, so two children are only
    // locked together under rename_lock. The trap checks above make sure
    // neither is an ancestor of the other
    Vnode *child1 = NULL;
    Vnode *child2 = NULL;
    if (src_is_dir) {
        child1 = cross ? src : dst;
        child2 = cross ? dst : NULL;
        if (child2 != NULL && (uptr)child2 < (uptr)child1) {
            child1 = dst;
            child2 = src;
        }
        if (child1 != NULL) lock_two(child1, child2);
    }

    // See MOUNTS. A source that is not locked may become a mount point
    // concurrently, which just moves the mount with it
    if (vfs_mounted(src) != NULL || src->isroot ||
        (dst != NULL && (vfs_mounted(dst) != NULL || dst->isroot))) {
        ret = EBUSY;
        goto unlock_children;
    }

    ret = srcdir->ops->rename(srcdir, srcname, srcnamelen, src, dstdir, dstname,
                              dstnamelen, dst);
    if (ret == 0 && dst != NULL && src_is_dir) dst->dead = true;

unlock_children:
    if (child1 != NULL) unlock_two(child1, child2);

unlock:
    unlock_two(first, second);
    if (cross) mutex_rel(&vfs->rename_lock);

    // See LOCKING
    vfs_vnode_rel(src);
    vfs_vnode_rel(dst);
    vfs_vnode_rel(src_trap);
    vfs_vnode_rel(dst_trap);
    return ret;
}

int vfs_readdir(void *buf, usize buflen, Vnode *dir, u64 off,
                usize *out_bytes_read, u64 *out_next_off) {
    *out_bytes_read = 0;
    *out_next_off = off;
    if (!is_dir(dir)) return ENOTDIR;
    if (dir->ops->readdir == NULL) return ENOSYS;

    int ret = 0;
    rwlock_acq_read(&dir->lock);
    if (!dir->dead) {
        ret = dir->ops->readdir(buf, buflen, dir, off, out_bytes_read,
                                out_next_off);
    }
    rwlock_rel_read(&dir->lock);
    return ret;
}

int vfs_readlink(char *buf, usize buflen, Vnode *link, usize *out_len) {
    *out_len = 0;
    if (link->type != VNODE_TYPE_LINK) return EINVAL;
    if (link->ops->readlink == NULL) return ENOSYS;

    return link->ops->readlink(buf, buflen, link, out_len);
}

// BINDINGS

int vfs_bind(Vnode *node, void *binding) {
    assert(!is_dir(node) && node->type != VNODE_TYPE_LINK);
    assert(binding != NULL);

    int ret = 0;
    rwlock_acq_write(&node->lock);
    if (node->binding != NULL) {
        ret = EADDRINUSE;
    } else {
        vfs_vnode_hold(node);
        node->binding = binding;
    }
    rwlock_rel_write(&node->lock);
    return ret;
}

void vfs_unbind(Vnode *node, void *binding) {
    rwlock_acq_write(&node->lock);
    assert(binding == node->binding);
    node->binding = NULL;
    rwlock_rel_write(&node->lock);
    (void)binding;
    vfs_vnode_rel(node);
}

void *vfs_binding_acq(Vnode *node) {
    rwlock_acq_read(&node->lock);
    return node->binding;
}

void vfs_binding_rel(Vnode *node) { rwlock_rel_read(&node->lock); }

// DATA AND ATTRIBUTES

// A node without read falls back to readv, and vice versa
int vfs_read(void *buf, usize buflen, Vnode *node, u64 off, u64 flags,
             usize *out_bytes_read) {
    usize dump = 0;
    if (out_bytes_read == NULL) out_bytes_read = &dump;

    *out_bytes_read = 0;
    if (node->ops->read == NULL) {
        if (node->ops->readv != NULL) {
            struct iovec iov = {.iov_base = buf, .iov_len = buflen};
            IOVIter iter;
            ioviter_init(&iter, &iov, 1);
            return node->ops->readv(&iter, node, off, flags, out_bytes_read);
        }

        return ENOSYS;
    }

    return node->ops->read(buf, buflen, node, off, flags, out_bytes_read);
}

int vfs_write(Vnode *node, void *buf, usize buflen, u64 off, u64 flags,
              usize *out_bytes_written) {
    usize dump = 0;
    if (out_bytes_written == NULL) out_bytes_written = &dump;

    *out_bytes_written = 0;
    if (vfs_readonly(node)) return EROFS;

    if (node->ops->write == NULL) {
        if (node->ops->writev != NULL) {
            struct iovec iov = {.iov_base = buf, .iov_len = buflen};
            IOVIter iter;
            ioviter_init(&iter, &iov, 1);
            return node->ops->writev(node, &iter, off, flags,
                                     out_bytes_written);
        }

        return ENOSYS;
    }

    return node->ops->write(node, buf, buflen, off, flags, out_bytes_written);
}

int vfs_readv(IOVIter *ioviter, Vnode *node, u64 off, u64 flags,
              usize *out_bytes_read) {
    usize dump = 0;
    if (out_bytes_read == NULL) out_bytes_read = &dump;

    if (node->ops->readv != NULL) {
        return node->ops->readv(ioviter, node, off, flags, out_bytes_read);
    }

    if (node->ops->read == NULL) return ENOSYS;

    struct iovec *iov;
    int ret = 0;
    usize tot_read = 0;

    for (usize iov_off = off; (iov = ioviter_next(ioviter)) != NULL;) {
        usize curr_read = 0;
        ret = node->ops->read(iov->iov_base, iov->iov_len, node, iov_off, flags,
                              &curr_read);
        iov_off += curr_read;
        tot_read += curr_read;
        if (ret != 0 || curr_read < iov->iov_len) break;
    }

    *out_bytes_read = tot_read;
    return tot_read != 0 ? 0 : ret;
}

int vfs_writev(Vnode *node, IOVIter *ioviter, u64 off, u64 flags,
               usize *out_bytes_written) {
    usize dump = 0;
    if (out_bytes_written == NULL) out_bytes_written = &dump;

    *out_bytes_written = 0;
    if (vfs_readonly(node)) return EROFS;

    if (node->ops->writev != NULL) {
        return node->ops->writev(node, ioviter, off, flags, out_bytes_written);
    }

    if (node->ops->write == NULL) return ENOSYS;

    struct iovec *iov;
    int ret = 0;
    usize tot_write = 0;

    for (usize iov_off = off; (iov = ioviter_next(ioviter)) != NULL;) {
        usize curr_write = 0;
        ret = node->ops->write(node, iov->iov_base, iov->iov_len, iov_off,
                               flags, &curr_write);
        iov_off += curr_write;
        tot_write += curr_write;
        if (ret != 0 || curr_write < iov->iov_len) break;
    }

    *out_bytes_written = tot_write;
    return tot_write != 0 ? 0 : ret;
}

int vfs_truncate(Vnode *node, usize size) {
    if (is_dir(node)) return EISDIR;
    if (vfs_readonly(node)) return EROFS;
    if (node->ops->truncate == NULL) return EINVAL;

    return node->ops->truncate(node, size);
}

int vfs_mmap(Vnode *node, VMMContext *vmm_context, void *hint, usize size,
             int vmm_flags, MMUFlags mmu_flags, u64 off, void **out_addr) {
    if ((vmm_flags & VMM_FLAGS_SHARED) && (mmu_flags & MMU_FLAGS_WRITE) &&
        vfs_readonly(node)) {
        return EROFS;
    }

    if (node->ops->mmap == NULL) return ENOSYS;

    void *dump;
    if (out_addr == NULL) out_addr = &dump;

    return node->ops->mmap(node, vmm_context, hint, size, vmm_flags, mmu_flags,
                           off, out_addr);
}

int vfs_stat(Vnode *node, bool strict, struct stat *out_stat) {
    static const mode_t types[] = {
        [VNODE_TYPE_FILE] = S_IFREG,     [VNODE_TYPE_DIR] = S_IFDIR,
        [VNODE_TYPE_LINK] = S_IFLNK,     [VNODE_TYPE_CHARDEV] = S_IFCHR,
        [VNODE_TYPE_FIFO] = S_IFIFO,     [VNODE_TYPE_SOCKET] = S_IFSOCK,
        [VNODE_TYPE_BLOCKDEV] = S_IFBLK,
    };

    memset(out_stat, 0, sizeof(struct stat));
    int ret = node->ops->stat != NULL ? node->ops->stat(node, out_stat)
                                      : ENOSYS;
    if (ret == ENOSYS && !strict) ret = 0;
    if (ret == 0) {
        out_stat->st_dev = node->vfs != NULL ? node->vfs->dev : 0;
        out_stat->st_ino = node->ino;
        out_stat->st_mode = (out_stat->st_mode & ~S_IFMT) | types[node->type];
    }
    return ret;
}

int vfs_isatty(Vnode *node) {
    if (node->ops->isatty == NULL) return ENOSYS;

    return node->ops->isatty(node);
}

int vfs_seekable(Vnode *node) {
    if (node->ops->seekable == NULL) return 0;

    return node->ops->seekable(node);
}

int vfs_ioctl(Vnode *node, u64 op, void *buf) {
    if (node->ops->ioctl == NULL) return ENOSYS;

    return node->ops->ioctl(node, op, buf);
}

int vfs_poll_head(Vnode *node, struct PollHead **out_poll_head) {
    if (node->ops->poll_head == NULL) return ENOSYS;

    return node->ops->poll_head(node, out_poll_head);
}

int vfs_poll(Vnode *node, short events, short *out_revents) {
    if (node->ops->poll == NULL) return ENOSYS;

    return node->ops->poll(node, events, out_revents);
}

// UTILITY FUNCTIONS

bool vfs_readonly(Vnode *node) {
    return node->vfs != NULL &&
        (__atomic_load_n(&node->vfs->flags, __ATOMIC_RELAXED) &
         VFS_MOUNT_RDONLY) &&
        (node->type == VNODE_TYPE_FILE || is_dir(node) ||
         node->type == VNODE_TYPE_LINK);
}

ino_t vfs_new_ino(void) {
    static ino_t next = 1;
    return __atomic_fetch_add(&next, 1, __ATOMIC_RELAXED);
}
