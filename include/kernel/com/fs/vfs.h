#pragma once

// NOTE: declared before the includes, because <kernel/com/sync/mutex.h>
// includes <kernel/com/sys/proc.h>, which includes this header and needs these
// names
typedef struct Vnode Vnode;
typedef struct Vfs Vfs;
struct PollHead;

#include <kernel/com/ds/ioviter.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sync/mutex.h>
#include <kernel/com/sync/rwlock.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/util.h>
#include <sys/stat.h>
#include <vendor/tailq.h>

// File systems only ever see single names, never paths. The walker in vfs.c is
// the only place that knows about ".", "..", mount points and symbolic links,
// and the Vfs locks directories and does every check that is the same for all
// file systems before calling them.
//
// LIFETIME:
//      1. Every function that outputs a vnode gives the caller a reference,
//      dropped with vfs_vnode_rel
//      2. A new reference is only taken from an existing one, or under a lock
//      that keeps one stable (e.g., a directory's lock for its entries)
//      3. When the count reaches 0, inactive frees the vnode
//      4. close is called once per open file description, when the file is
//      freed. It undoes open, and shall not free the vnode
//      5. References owned by a file system's own structures (e.g., tmpfs
//      directory entries) are internal and use the _INTERNAL macros. All the
//      others are external, and are also counted by vfs->num_ext
//      6. Releases may sleep, so they are never done under a spinlock
//
// LOCKING: every vnode has a Vfs-owned rwlock. For directories it protects the
// entries and the dead flag, for sockets and FIFOs the binding. File systems
// never take vnode locks, though they may use internal locks to protect calls
// that are not synchronized by the Vfs layer (e.g., read, write, ioctl, etc.)
// The Vfs only releases vnodes after unlocking, since a last release may sleep
// for I/O (see VNODE CACHE)
//
// LOCK ORDER: rename_lock, then directories, ancestor before descendant. Two
// unrelated directories are only locked together by rename, under rename_lock,
// in address order. Binding locks are never held with other vnode locks. The
// vnode cache lock comes after every vnode lock
//
// MOUNTS: mountpointof and vfs->mountpoint are written with the mount point's
// lock held in write mode, and read in read mode to cross the mount point.
// vfs_mounted reads it without the lock as a hint, so it is stored
// atomically
//
// UNMOUNT: while unmount holds the mount point's lock, nobody can enter the
// file system, and new external references only come from existing ones. So if
// num_ext == 2 (vfs->root's and the caller's), nothing else can reach it. Since
// file systems are only freed then, an external reference also keeps its vfs
// (and vfs->mountpoint) valid

#define VFS_NAME_MAX 255

#define VFS_MOUNT_RDONLY 1

typedef enum VnodeType {
    VNODE_TYPE_FILE,
    VNODE_TYPE_DIR,
    VNODE_TYPE_LINK,
    VNODE_TYPE_CHARDEV,
    VNODE_TYPE_FIFO,
    VNODE_TYPE_SOCKET,
    VNODE_TYPE_BLOCKDEV
} VnodeType;

struct Vfs {
    struct FSType *type;
    struct Vnode *mountpoint; // owns a reference
    struct Vnode *root;       // owns a reference
    struct Vnode *backing;    // open and bound by the Vfs, or NULL
    void *extra;
    dev_t dev;
    u64 flags; // VFS_MOUNT_*
    u64 num_ext;
    Mutex rename_lock;
    struct VFSCache *cache; // NULL unless vfs_cache_init
};

struct Vnode {
    struct VnodeOps *ops;
    struct Vfs *vfs; // NULL for pipes and sockets
    VnodeType type;
    ino_t ino;
    u64 num_ref;
    void *extra;
    struct Vfs *mountpointof;
    bool isroot;

    // Cached vnodes only, see VNODE CACHE
    bool cached; // came from vfs_vget
    TAILQ_ENTRY(Vnode) lru_link;

    // Protected by lock. dead is only set on removed directories, and a
    // binding owns a reference to the vnode
    RWLock lock;
    bool dead;
    void *binding;
};

static inline struct Vfs *vfs_mounted(Vnode *node) {
    return __atomic_load_n(&node->mountpointof, __ATOMIC_RELAXED);
}

typedef struct VnodeAttr {
    VnodeType type;
    mode_t mode;
    const char *target; // symbolic links only
    usize targetlen;
    const void *fsdata; // file system defined, never from user input
} VnodeAttr;

#define VFS_ATTR(t) ((VnodeAttr){.type = (t)})

// Operations are called with their vnodes referenced and no vnode locks held,
// unless stated otherwise. A NULL operation makes the Vfs function fail with
// ENOSYS. Name operations never see dot names or dead directories, and are
// called after every check listed for the Vfs function

typedef struct VnodeOps {
    // NULL means that the open file uses node itself
    int (*open)(Vnode *node, Vnode **out_vnode);
    // See LIFETIME.4. May run while other references exist
    int (*close)(Vnode *node);
    // See LIFETIME.3. Also called on vnodes that were never opened
    void (*inactive)(Vnode *node);

    // LOCKING: dir->lock held in either mode. name may be ".."
    int (*lookup)(Vnode *dir, const char *name, usize namelen,
                  Vnode **out_vnode);
    // LOCKING: dir->lock held in write mode, name does not exist in dir
    // post: \result != 0 ==> name and any new inode are gone, as if never
    // created
    int (*create)(Vnode *dir, const char *name, usize namelen,
                  const VnodeAttr *attr, Vnode **out_vnode);
    // LOCKING: dir->lock held in write mode, name does not exist in dir
    // post: \result == ENOENT ==> node has no links left
    int (*link)(Vnode *dir, const char *name, usize namelen, Vnode *node);
    // LOCKING: dir->lock held in write mode, and node->lock if it is a
    // directory. name links to node, which is neither a mount point nor a root
    int (*unlink)(Vnode *dir, const char *name, usize namelen, Vnode *node);
    // LOCKING: srcdir->lock and dstdir->lock held in write mode, plus
    // rename_lock if they differ. The locks of replaced and node are also held
    // if they are directories (node's only if its parent changes)
    // pre: node is srcname, replaced is dstname (or NULL), both of the same
    // type class, neither a mount point, a root, or an ancestor of the other
    // directory
    // NOTE: shall be atomic, dstname never stops existing
    int (*rename)(Vnode *srcdir, const char *srcname, usize srcnamelen,
                  Vnode *node, Vnode *dstdir, const char *dstname,
                  usize dstnamelen, Vnode *replaced);
    // LOCKING: dir->lock held in read mode
    // post: one entry in buf, or *bytes_read == 0 at the end. Offsets are
    // opaque, 0 is the start, *next_off is the next one. buf may be a user
    // address
    int (*readdir)(void *buf, usize buflen, Vnode *dir, u64 off,
                   usize *out_bytes_read, u64 *out_next_off);
    // post: *len is the full length, buf holds at most buflen bytes of it
    int (*readlink)(char *buf, usize buflen, Vnode *link, usize *out_len);

    int (*read)(void *buf, usize buflen, Vnode *node, u64 off, u64 flags,
                usize *out_bytes_read);
    int (*write)(Vnode *node, void *buf, usize buflen, u64 off, u64 flags,
                 usize *out_bytes_written);
    int (*readv)(IOVIter *ioviter, Vnode *node, u64 off, u64 flags,
                 usize *out_bytes_read);
    int (*writev)(Vnode *node, IOVIter *ioviter, u64 off, u64 flags,
                  usize *out_bytes_written);
    int (*truncate)(Vnode *node, usize size);
    int (*mmap)(Vnode *node, VMMContext *vmm_context, void *hint, usize size,
                int vmm_flags, MMUFlags mmu_flags, u64 off, void **out_addr);

    // Optional; out is zeroed, Vfs sets st_dev, st_ino and type bits afterwards
    int (*stat)(Vnode *node, struct stat *out_stat);
    int (*isatty)(Vnode *node);
    int (*seekable)(Vnode *node);
    int (*ioctl)(Vnode *node, u64 op, void *buf);
    int (*poll_head)(Vnode *node, struct PollHead **out_poll_head);
    int (*poll)(Vnode *node, short events, short *out_revents);
} VnodeOps;

typedef struct FSType {
    TAILQ_ENTRY(FSType) types_link;
    const char *name;
    bool needs_backing;

    // backing is NULL unless needs_backing, and is owned by the Vfs
    // post: \result == 0 ==> *out was set up with vfs_init_vfs and
    // vfs_set_root
    int (*mount)(Vnode *backing, u64 flags, Vfs **out_vfs);
    // LOCKING: the mount point's lock held in write mode
    // pre: num_ext == 2 (see UNMOUNT). The file system drops both references
    // to its root
    // post: \result == 0 ==> every vnode and vfs are freed, otherwise nothing
    // changed
    int (*unmount)(Vfs *vfs);

    // Cached file systems only, see VNODE CACHE
    // post: \result == 0 ==> *out is a new vnode for ino, made with
    // vfs_vnode_init
    int (*vget)(Vfs *vfs, ino_t ino, Vnode **out_vnode);
    // Called under the cache lock when the last reference to node is dropped.
    // false reclaims it at once (e.g., it has no links left)
    bool (*keep)(Vnode *node);
} FSType;

typedef struct VFSDirEnt {
    ino_t ino;
    off_t off; // the offset of the next entry
    unsigned short reclen;
    unsigned char type;
    char name[];
} VFSDirEnt;

// LIFETIME

// post: vnode has one external reference, owned by the caller
void vfs_vnode_init(Vnode *vnode, Vfs *vfs, VnodeType type, VnodeOps *ops,
                    void *extra);
// Only called by the RELEASE macros
void vfs_inactive(Vnode *vnode);
// Only called by the RELEASE macros, for vnodes of cached file systems
void vfs_cache_rel(Vnode *node);

static inline void vfs_vnode_hold(Vnode *node) {
    if (node->vfs != NULL) {
        __atomic_add_fetch(&node->vfs->num_ext, 1, __ATOMIC_RELAXED);
    }
    __atomic_add_fetch(&node->num_ref, 1, __ATOMIC_RELAXED);
}

static inline void vfs_vnode_hold_internal(Vnode *node) {
    __atomic_add_fetch(&node->num_ref, 1, __ATOMIC_RELAXED);
}

static inline void vfs_vnode_rel_internal(Vnode *node) {
    if (node->vfs != NULL && node->vfs->cache != NULL) {
        vfs_cache_rel(node);
    } else if (__atomic_add_fetch(&node->num_ref, -1, __ATOMIC_ACQ_REL) == 0) {
        vfs_inactive(node);
    }
}

// num_ext is decremented last, so that the file system cannot be unmounted
// while the vnode is being freed
static inline void vfs_vnode_rel(Vnode *node) {
    if (node == NULL) return;

    Vfs *vfs = node->vfs;
    vfs_vnode_rel_internal(node);
    if (vfs != NULL) __atomic_add_fetch(&vfs->num_ext, -1, __ATOMIC_RELEASE);
}

int vfs_open(Vnode *node, Vnode **out_vnode);
int vfs_close(Vnode *node);

// MOUNTS

// post: \result == EEXIST ==> a type with the same name exists
int vfs_register_type(FSType *type);
FSType *vfs_find_type(const char *name, usize namelen);

// For file systems, before creating any vnode of vfs
void vfs_init_vfs(Vfs *vfs, FSType *type, void *extra);
// The reference to root becomes vfs's
void vfs_set_root(Vfs *vfs, Vnode *root);

// Mounts a new file system of type on mountpoint, or nowhere if NULL (root).
// source is only used if type->needs_backing, and backs one file system at most
// post: \result == ENOTBLK ==> source is not a file or block device
// post: \result == EROFS ==> source is on a read-only mount, and flags do not
// include VFS_MOUNT_RDONLY
// post: \result == EBUSY ==> source is in use, or mountpoint is a mount point
// or a root
int vfs_mount(FSType *type, Vnode *source, Vnode *mountpoint, u64 flags,
              Vfs **out_vfs);
// Consumes the caller's reference to root
// post: \result == EINVAL ==> root is not the root of a mounted file system
// post: \result == EBUSY ==> the file system is in use
int vfs_unmount(Vnode *root);

// VNODE CACHE
//
// Keeps one vnode per inode number, for file systems that load vnodes from
// storage (e.g., ext2). Such file systems only create vnodes in
// vfs->type->vget, and get them (e.g., in lookup) with vfs_vget. A vnode
// whose last reference is dropped is parked, unless keep says otherwise, and
// the oldest parked vnodes are reclaimed (i.e., inactive is called) above
// max_parked

int vfs_cache_init(Vfs *vfs, usize max_parked);
// For unmount, once the root is released: reclaims every parked vnode
void vfs_cache_destroy(Vfs *vfs);
int vfs_vget(Vfs *vfs, ino_t ino, Vnode **out_vnode);

// PATHS
//
// root is the process root, which ".." never leaves, cwd is where relative
// paths start from. A path that ends with '/' only resolves to a directory

#define VFS_LOOKUP_FOLLOW 1 // follow a symbolic link at the end
#define VFS_CREATE_EXCL   2 // fail with EEXIST if the name exists

int vfs_lookup(const char *path, usize pathlen, Vnode *root, Vnode *cwd,
               int flags, Vnode **out_vnode);
// post: \result == 0 ==> *out is the directory that contains the last
// component of path, *name points into path, *must_dir tells whether path
// ends with '/'. *namelen == 0 if path only has slashes
int vfs_lookup_parent(const char *path, usize pathlen, Vnode *root, Vnode *cwd,
                      Vnode **out_vnode, const char **out_name,
                      usize *out_namelen, bool *out_must_dir);
// Creates path as open does with O_CREAT. Without VFS_CREATE_EXCL, an
// existing vnode is returned, and VFS_LOOKUP_FOLLOW creates the target
// of a symbolic link
int vfs_create_path(const char *path, usize pathlen, Vnode *root, Vnode *cwd,
                    const VnodeAttr *attr, int flags, Vnode **out_vnode);
// Builds the path from root to dir in the end of buf, starting at *start
// NOTE: uses readdir and stat on the parents, so it works on any file system
int vfs_get_path(char *buf, usize size, Vnode *dir, Vnode *root,
                 usize *out_start);

// NAMES
//
// Name operations fail with EROFS on read-only mounts, ENOTDIR if a directory
// argument is not one, ENAMETOOLONG above VFS_NAME_MAX, and ENOENT if
// the directory a name is added to is dead

// out may be NULL
// post: \result == EEXIST ==> name is a dot name or exists
int vfs_create(Vnode *dir, const char *name, usize namelen,
               const VnodeAttr *attr, Vnode **out_vnode);
// post: \result == EPERM ==> node is a directory
// post: \result == EXDEV ==> node is on another file system
int vfs_link(Vnode *dir, const char *name, usize namelen, Vnode *node);

#define VFS_REMOVE_DIR 1 // rmdir
#define VFS_MUST_DIR   2 // the path ended with '/'

// A directory removed this way is dead
// post: \result == EBUSY ==> name is a mount point
int vfs_unlink(Vnode *dir, const char *name, usize namelen, int flags);
// post: \result == EINVAL ==> the source is an ancestor of dstdir
// post: \result == ENOTEMPTY ==> the target is a non-empty directory, or an
// ancestor of srcdir
int vfs_rename(Vnode *srcdir, const char *srcname, usize srcnamelen,
               Vnode *dstdir, const char *dstname, usize dstnamelen, int flags);
// A dead directory reads as empty
int vfs_readdir(void *buf, usize buflen, Vnode *dir, u64 off,
                usize *out_bytes_read, u64 *out_next_off);
int vfs_readlink(char *buf, usize buflen, Vnode *link, usize *out_len);

// BINDINGS
//
// A binding attaches a kernel object to a vnode: a listening socket, or the
// file system type a device backs. Bindings only exist in this kernel, so they
// belong to the Vfs, not to file systems

// post: \result == EADDRINUSE ==> node is already bound
int vfs_bind(Vnode *node, void *binding);
void vfs_unbind(Vnode *node, void *binding);
// The binding cannot be removed until vfs_binding_rel
void *vfs_binding_acq(Vnode *node);
void vfs_binding_rel(Vnode *node);

// DATA AND ATTRIBUTES

int vfs_read(void *buf, usize buflen, Vnode *node, u64 off, u64 flags,
             usize *out_bytes_read);
int vfs_write(Vnode *node, void *buf, usize buflen, u64 off, u64 flags,
              usize *out_bytes_written);
int vfs_readv(IOVIter *ioviter, Vnode *node, u64 off, u64 flags,
              usize *out_bytes_read);
int vfs_writev(Vnode *node, IOVIter *ioviter, u64 off, u64 flags,
               usize *out_bytes_written);
int vfs_truncate(Vnode *node, usize size);
int vfs_mmap(Vnode *node, VMMContext *vmm_context, void *hint, usize size,
             int vmm_flags, MMUFlags mmu_flags, u64 off, void **out_addr);
// If strict, ENOSYS without a stat operation. Otherwise, Vfs fields only
int vfs_stat(Vnode *node, bool strict, struct stat *out_stat);
int vfs_isatty(Vnode *node);
int vfs_seekable(Vnode *node);
int vfs_ioctl(Vnode *node, u64 op, void *buf);
int vfs_poll_head(Vnode *node, struct PollHead **out_poll_head);
int vfs_poll(Vnode *node, short events, short *out_revents);

// UTILITY FUNCTIONS

// Files, directories and links on a read-only mount
// A false result may be stale. A file system that can become read-only by
// itself shall recheck under its own locks and fail with EROFS
bool vfs_readonly(Vnode *node);
ino_t vfs_new_ino(void);
