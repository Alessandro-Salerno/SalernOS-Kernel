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

#pragma once

// NOTE: declared before the includes, because <lib/mutex.h> includes
// <kernel/com/sys/proc.h>, which includes this header and needs these names
typedef struct com_vnode com_vnode_t;
typedef struct com_vfs   com_vfs_t;
struct com_poll_head;

#include <kernel/com/mm/vmm.h>
#include <lib/ioviter.h>
#include <lib/mutex.h>
#include <lib/rwlock.h>
#include <lib/spinlock.h>
#include <lib/util.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <vendor/tailq.h>

// File systems only ever see single names, never paths. The walker in vfs.c is
// the only place that knows about ".", "..", mount points and symbolic links,
// and the VFS locks directories and does every check that is the same for all
// file systems before calling them.
//
// LIFETIME:
//      1. Every function that outputs a vnode gives the caller a reference,
//      dropped with COM_FS_VFS_VNODE_RELEASE
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
// LOCKING: every vnode has a VFS-owned rwlock. For directories it protects the
// entries and the dead flag, for sockets and FIFOs the binding. File systems
// never take vnode locks, though they may use internal locks to protect calls
// that are not synchronized by the VFS layer (e.g., read, write, ioctl, etc.)
// The VFS only releases vnodes after unlocking, since a last release may sleep
// for I/O (see VNODE CACHE)
//
// LOCK ORDER: rename_lock, then directories, ancestor before descendant. Two
// unrelated directories are only locked together by rename, under rename_lock,
// in address order. Binding locks are never held with other vnode locks. The
// vnode cache lock comes after every vnode lock
//
// MOUNTS: mountpointof and vfs->mountpoint are written with the mount point's
// lock held in write mode, and read in read mode to cross the mount point.
// COM_FS_VFS_MOUNTED reads it without the lock as a hint, so it is stored
// atomically
//
// UNMOUNT: while unmount holds the mount point's lock, nobody can enter the
// file system, and new external references only come from existing ones. So if
// num_ext == 2 (vfs->root's and the caller's), nothing else can reach it. Since
// file systems are only freed then, an external reference also keeps its vfs
// (and vfs->mountpoint) valid

#define COM_FS_VFS_NAME_MAX 255

#define COM_FS_VFS_VNODE_HOLD(node)    com_fs_vfs_vnode_hold(node)
#define COM_FS_VFS_VNODE_RELEASE(node) com_fs_vfs_vnode_release(node)
#define COM_FS_VFS_VNODE_HOLD_INTERNAL(node) \
    com_fs_vfs_vnode_hold_internal(node)
#define COM_FS_VFS_VNODE_RELEASE_INTERNAL(node) \
    com_fs_vfs_vnode_release_internal(node)

#define COM_FS_VFS_MOUNTED(node) \
    __atomic_load_n(&(node)->mountpointof, __ATOMIC_RELAXED)

#define COM_FS_VFS_MOUNT_RDONLY 1

typedef enum com_vnode_type {
    E_COM_VNODE_TYPE_FILE,
    E_COM_VNODE_TYPE_DIR,
    E_COM_VNODE_TYPE_LINK,
    E_COM_VNODE_TYPE_CHARDEV,
    E_COM_VNODE_TYPE_FIFO,
    E_COM_VNODE_TYPE_SOCKET,
    E_COM_VNODE_TYPE_BLOCKDEV
} com_vnode_type_t;

struct com_vfs {
    struct com_fs_type   *type;
    struct com_vnode     *mountpoint; // owns a reference
    struct com_vnode     *root;       // owns a reference
    struct com_vnode     *backing;    // open and bound by the VFS, or NULL
    void                 *extra;
    dev_t                 dev;
    uintmax_t             flags; // COM_FS_VFS_MOUNT_*
    uintmax_t             num_ext;
    kmutex_t              rename_lock;
    struct com_vfs_cache *cache; // NULL unless com_fs_vfs_cache_init
};

struct com_vnode {
    struct com_vnode_ops *ops;
    struct com_vfs       *vfs; // NULL for pipes and sockets
    com_vnode_type_t      type;
    ino_t                 ino;
    uintmax_t             num_ref;
    void                 *extra;
    struct com_vfs       *mountpointof;
    bool                  isroot;

    // Cached vnodes only, see VNODE CACHE
    bool cached; // came from com_fs_vfs_vget
    TAILQ_ENTRY(com_vnode) lru_link;

    // Protected by lock. dead is only set on removed directories, and a
    // binding owns a reference to the vnode
    krwlock_t lock;
    bool      dead;
    void     *binding;
};

typedef struct com_vnode_attr {
    com_vnode_type_t type;
    mode_t           mode;
    const char      *target; // symbolic links only
    size_t           targetlen;
    const void      *fsdata; // file system defined, never from user input
} com_vnode_attr_t;

#define COM_FS_VFS_ATTR(t) ((com_vnode_attr_t){.type = (t)})

// Operations are called with their vnodes referenced and no vnode locks held,
// unless stated otherwise. A NULL operation makes the VFS function fail with
// ENOSYS. Name operations never see dot names or dead directories, and are
// called after every check listed for the VFS function

typedef struct com_vnode_ops {
    // NULL means that the open file uses node itself
    int (*open)(com_vnode_t **out, com_vnode_t *node);
    // See LIFETIME.4. May run while other references exist
    int (*close)(com_vnode_t *node);
    // See LIFETIME.3. Also called on vnodes that were never opened
    void (*inactive)(com_vnode_t *node);

    // LOCKING: dir->lock held in either mode. name may be ".."
    int (*lookup)(com_vnode_t **out,
                  com_vnode_t  *dir,
                  const char   *name,
                  size_t        namelen);
    // LOCKING: dir->lock held in write mode, name does not exist in dir
    // post: \result != 0 ==> name and any new inode are gone, as if never
    // created
    int (*create)(com_vnode_t           **out,
                  com_vnode_t            *dir,
                  const char             *name,
                  size_t                  namelen,
                  const com_vnode_attr_t *attr);
    // LOCKING: dir->lock held in write mode, name does not exist in dir
    // post: \result == ENOENT ==> node has no links left
    int (*link)(com_vnode_t *dir,
                const char  *name,
                size_t       namelen,
                com_vnode_t *node);
    // LOCKING: dir->lock held in write mode, and node->lock if it is a
    // directory. name links to node, which is neither a mount point nor a root
    int (*unlink)(com_vnode_t *dir,
                  const char  *name,
                  size_t       namelen,
                  com_vnode_t *node);
    // LOCKING: srcdir->lock and dstdir->lock held in write mode, plus
    // rename_lock if they differ. The locks of replaced and node are also held
    // if they are directories (node's only if its parent changes)
    // pre: node is srcname, replaced is dstname (or NULL), both of the same
    // type class, neither a mount point, a root, or an ancestor of the other
    // directory
    // NOTE: shall be atomic, dstname never stops existing
    int (*rename)(com_vnode_t *srcdir,
                  const char  *srcname,
                  size_t       srcnamelen,
                  com_vnode_t *node,
                  com_vnode_t *dstdir,
                  const char  *dstname,
                  size_t       dstnamelen,
                  com_vnode_t *replaced);
    // LOCKING: dir->lock held in read mode
    // post: one entry in buf, or *bytes_read == 0 at the end. Offsets are
    // opaque, 0 is the start, *next_off is the next one. buf may be a user
    // address
    int (*readdir)(void        *buf,
                   size_t       buflen,
                   size_t      *bytes_read,
                   uintmax_t   *next_off,
                   com_vnode_t *dir,
                   uintmax_t    off);
    // post: *len is the full length, buf holds at most buflen bytes of it
    int (*readlink)(size_t *len, char *buf, size_t buflen, com_vnode_t *link);

    int (*read)(void        *buf,
                size_t       buflen,
                size_t      *bytes_read,
                com_vnode_t *node,
                uintmax_t    off,
                uintmax_t    flags);
    int (*write)(size_t      *bytes_written,
                 com_vnode_t *node,
                 void        *buf,
                 size_t       buflen,
                 uintmax_t    off,
                 uintmax_t    flags);
    int (*readv)(kioviter_t  *ioviter,
                 size_t      *bytes_read,
                 com_vnode_t *node,
                 uintmax_t    off,
                 uintmax_t    flags);
    int (*writev)(size_t      *bytes_written,
                  com_vnode_t *node,
                  kioviter_t  *ioviter,
                  uintmax_t    off,
                  uintmax_t    flags);
    int (*truncate)(com_vnode_t *node, size_t size);
    int (*mmap)(void             **out,
                com_vnode_t       *node,
                com_vmm_context_t *vmm_context,
                void              *hint,
                size_t             size,
                int                vmm_flags,
                arch_mmu_flags_t   mmu_flags,
                uintmax_t          off);

    // Optional; out is zeroed, VFS sets st_dev, st_ino and type bits afterwards
    int (*stat)(struct stat *out, com_vnode_t *node);
    int (*isatty)(com_vnode_t *node);
    int (*seekable)(com_vnode_t *node);
    int (*ioctl)(com_vnode_t *node, uintmax_t op, void *buf);
    int (*poll_head)(struct com_poll_head **out, com_vnode_t *node);
    int (*poll)(short *revents, com_vnode_t *node, short events);
} com_vnode_ops_t;

typedef struct com_fs_type {
    TAILQ_ENTRY(com_fs_type) types_link;
    const char *name;
    bool        needs_backing;

    // backing is NULL unless needs_backing, and is owned by the VFS
    // post: \result == 0 ==> *out was set up with com_fs_vfs_init_vfs and
    // com_fs_vfs_set_root
    int (*mount)(com_vfs_t **out, com_vnode_t *backing, uintmax_t flags);
    // LOCKING: the mount point's lock held in write mode
    // pre: num_ext == 2 (see UNMOUNT). The file system drops both references
    // to its root
    // post: \result == 0 ==> every vnode and vfs are freed, otherwise nothing
    // changed
    int (*unmount)(com_vfs_t *vfs);

    // Cached file systems only, see VNODE CACHE
    // post: \result == 0 ==> *out is a new vnode for ino, made with
    // com_fs_vfs_vnode_init
    int (*vget)(com_vnode_t **out, com_vfs_t *vfs, ino_t ino);
    // Called under the cache lock when the last reference to node is dropped.
    // false reclaims it at once (e.g., it has no links left)
    bool (*keep)(com_vnode_t *node);
} com_fs_type_t;

typedef struct com_dirent {
    ino_t          ino;
    off_t          off; // the offset of the next entry
    unsigned short reclen;
    unsigned char  type;
    char           name[];
} com_dirent_t;

// LIFETIME

// post: vnode has one external reference, owned by the caller
void com_fs_vfs_vnode_init(com_vnode_t     *vnode,
                           com_vfs_t       *vfs,
                           com_vnode_type_t type,
                           com_vnode_ops_t *ops,
                           void            *extra);
// Only called by the RELEASE macros
void com_fs_vfs_inactive(com_vnode_t *vnode);
// Only called by the RELEASE macros, for vnodes of cached file systems
void com_fs_vfs_cache_release(com_vnode_t *node);

static inline void com_fs_vfs_vnode_hold(com_vnode_t *node) {
    if (NULL != node->vfs) {
        __atomic_add_fetch(&node->vfs->num_ext, 1, __ATOMIC_RELAXED);
    }
    __atomic_add_fetch(&node->num_ref, 1, __ATOMIC_RELAXED);
}

static inline void com_fs_vfs_vnode_hold_internal(com_vnode_t *node) {
    __atomic_add_fetch(&node->num_ref, 1, __ATOMIC_RELAXED);
}

static inline void com_fs_vfs_vnode_release_internal(com_vnode_t *node) {
    if (NULL != node->vfs && NULL != node->vfs->cache) {
        com_fs_vfs_cache_release(node);
    } else if (0 == __atomic_add_fetch(&node->num_ref, -1, __ATOMIC_ACQ_REL)) {
        com_fs_vfs_inactive(node);
    }
}

// num_ext is decremented last, so that the file system cannot be unmounted
// while the vnode is being freed
static inline void com_fs_vfs_vnode_release(com_vnode_t *node) {
    if (NULL == node) {
        return;
    }

    com_vfs_t *vfs = node->vfs;
    com_fs_vfs_vnode_release_internal(node);
    if (NULL != vfs) {
        __atomic_add_fetch(&vfs->num_ext, -1, __ATOMIC_RELEASE);
    }
}

int com_fs_vfs_open(com_vnode_t **out, com_vnode_t *node);
int com_fs_vfs_close(com_vnode_t *node);

// MOUNTS

// post: \result == EEXIST ==> a type with the same name exists
int            com_fs_vfs_register_type(com_fs_type_t *type);
com_fs_type_t *com_fs_vfs_find_type(const char *name, size_t namelen);

// For file systems, before creating any vnode of vfs
void com_fs_vfs_init_vfs(com_vfs_t *vfs, com_fs_type_t *type, void *extra);
// The reference to root becomes vfs's
void com_fs_vfs_set_root(com_vfs_t *vfs, com_vnode_t *root);

// Mounts a new file system of type on mountpoint, or nowhere if NULL (root).
// source is only used if type->needs_backing, and backs one file system at most
// post: \result == ENOTBLK ==> source is not a file or block device
// post: \result == EROFS ==> source is on a read-only mount, and flags do not
// include COM_FS_VFS_MOUNT_RDONLY
// post: \result == EBUSY ==> source is in use, or mountpoint is a mount point
// or a root
int com_fs_vfs_mount(com_vfs_t    **out,
                     com_fs_type_t *type,
                     com_vnode_t   *source,
                     com_vnode_t   *mountpoint,
                     uintmax_t      flags);
// Consumes the caller's reference to root
// post: \result == EINVAL ==> root is not the root of a mounted file system
// post: \result == EBUSY ==> the file system is in use
int com_fs_vfs_unmount(com_vnode_t *root);

// VNODE CACHE
//
// Keeps one vnode per inode number, for file systems that load vnodes from
// storage (e.g., ext2). Such file systems only create vnodes in
// vfs->type->vget, and get them (e.g., in lookup) with com_fs_vfs_vget. A vnode
// whose last reference is dropped is parked, unless keep says otherwise, and
// the oldest parked vnodes are reclaimed (i.e., inactive is called) above
// max_parked

int com_fs_vfs_cache_init(com_vfs_t *vfs, size_t max_parked);
// For unmount, once the root is released: reclaims every parked vnode
void com_fs_vfs_cache_destroy(com_vfs_t *vfs);
int  com_fs_vfs_vget(com_vnode_t **out, com_vfs_t *vfs, ino_t ino);

// PATHS
//
// root is the process root, which ".." never leaves, cwd is where relative
// paths start from. A path that ends with '/' only resolves to a directory

#define COM_FS_VFS_LOOKUP_FOLLOW 1 // follow a symbolic link at the end
#define COM_FS_VFS_CREATE_EXCL   2 // fail with EEXIST if the name exists

int com_fs_vfs_lookup(com_vnode_t **out,
                      const char   *path,
                      size_t        pathlen,
                      com_vnode_t  *root,
                      com_vnode_t  *cwd,
                      int           flags);
// post: \result == 0 ==> *out is the directory that contains the last
// component of path, *name points into path, *must_dir tells whether path
// ends with '/'. *namelen == 0 if path only has slashes
int com_fs_vfs_lookup_parent(com_vnode_t **out,
                             const char  **name,
                             size_t       *namelen,
                             bool         *must_dir,
                             const char   *path,
                             size_t        pathlen,
                             com_vnode_t  *root,
                             com_vnode_t  *cwd);
// Creates path as open does with O_CREAT. Without COM_FS_VFS_CREATE_EXCL, an
// existing vnode is returned, and COM_FS_VFS_LOOKUP_FOLLOW creates the target
// of a symbolic link
int com_fs_vfs_create_path(com_vnode_t           **out,
                           const char             *path,
                           size_t                  pathlen,
                           com_vnode_t            *root,
                           com_vnode_t            *cwd,
                           const com_vnode_attr_t *attr,
                           int                     flags);
// Builds the path from root to dir in the end of buf, starting at *start
// NOTE: uses readdir and stat on the parents, so it works on any file system
int com_fs_vfs_get_path(size_t      *start,
                        char        *buf,
                        size_t       size,
                        com_vnode_t *dir,
                        com_vnode_t *root);

// NAMES
//
// Name operations fail with EROFS on read-only mounts, ENOTDIR if a directory
// argument is not one, ENAMETOOLONG above COM_FS_VFS_NAME_MAX, and ENOENT if
// the directory a name is added to is dead

// out may be NULL
// post: \result == EEXIST ==> name is a dot name or exists
int com_fs_vfs_create(com_vnode_t           **out,
                      com_vnode_t            *dir,
                      const char             *name,
                      size_t                  namelen,
                      const com_vnode_attr_t *attr);
// post: \result == EPERM ==> node is a directory
// post: \result == EXDEV ==> node is on another file system
int com_fs_vfs_link(com_vnode_t *dir,
                    const char  *name,
                    size_t       namelen,
                    com_vnode_t *node);

#define COM_FS_VFS_REMOVE_DIR 1 // rmdir
#define COM_FS_VFS_MUST_DIR   2 // the path ended with '/'

// A directory removed this way is dead
// post: \result == EBUSY ==> name is a mount point
int com_fs_vfs_unlink(com_vnode_t *dir,
                      const char  *name,
                      size_t       namelen,
                      int          flags);
// post: \result == EINVAL ==> the source is an ancestor of dstdir
// post: \result == ENOTEMPTY ==> the target is a non-empty directory, or an
// ancestor of srcdir
int com_fs_vfs_rename(com_vnode_t *srcdir,
                      const char  *srcname,
                      size_t       srcnamelen,
                      com_vnode_t *dstdir,
                      const char  *dstname,
                      size_t       dstnamelen,
                      int          flags);
// A dead directory reads as empty
int com_fs_vfs_readdir(void        *buf,
                       size_t       buflen,
                       size_t      *bytes_read,
                       uintmax_t   *next_off,
                       com_vnode_t *dir,
                       uintmax_t    off);
int com_fs_vfs_readlink(size_t      *len,
                        char        *buf,
                        size_t       buflen,
                        com_vnode_t *link);

// BINDINGS
//
// A binding attaches a kernel object to a vnode: a listening socket, or the
// file system type a device backs. Bindings only exist in this kernel, so they
// belong to the VFS, not to file systems

// post: \result == EADDRINUSE ==> node is already bound
int  com_fs_vfs_bind(com_vnode_t *node, void *binding);
void com_fs_vfs_unbind(com_vnode_t *node, void *binding);
// The binding cannot be removed until com_fs_vfs_binding_release
void *com_fs_vfs_binding_acquire(com_vnode_t *node);
void  com_fs_vfs_binding_release(com_vnode_t *node);

// DATA AND ATTRIBUTES

int com_fs_vfs_read(void        *buf,
                    size_t       buflen,
                    size_t      *bytes_read,
                    com_vnode_t *node,
                    uintmax_t    off,
                    uintmax_t    flags);
int com_fs_vfs_write(size_t      *bytes_written,
                     com_vnode_t *node,
                     void        *buf,
                     size_t       buflen,
                     uintmax_t    off,
                     uintmax_t    flags);
int com_fs_vfs_readv(kioviter_t  *ioviter,
                     size_t      *bytes_read,
                     com_vnode_t *node,
                     uintmax_t    off,
                     uintmax_t    flags);
int com_fs_vfs_writev(size_t      *bytes_written,
                      com_vnode_t *node,
                      kioviter_t  *ioviter,
                      uintmax_t    off,
                      uintmax_t    flags);
int com_fs_vfs_truncate(com_vnode_t *node, size_t size);
int com_fs_vfs_mmap(void             **out,
                    com_vnode_t       *node,
                    com_vmm_context_t *vmm_context,
                    void              *hint,
                    size_t             size,
                    int                vmm_flags,
                    arch_mmu_flags_t   mmu_flags,
                    uintmax_t          off);
// If strict, ENOSYS without a stat operation. Otherwise, VFS fields only
int com_fs_vfs_stat(struct stat *out, com_vnode_t *node, bool strict);
int com_fs_vfs_isatty(com_vnode_t *node);
int com_fs_vfs_seekable(com_vnode_t *node);
int com_fs_vfs_ioctl(com_vnode_t *node, uintmax_t op, void *buf);
int com_fs_vfs_poll_head(struct com_poll_head **out, com_vnode_t *node);
int com_fs_vfs_poll(short *revents, com_vnode_t *node, short events);

// UTILITY FUNCTIONS

// Files, directories and links on a read-only mount
// A false result may be stale. A file system that can become read-only by
// itself shall recheck under its own locks and fail with EROFS
bool  com_fs_vfs_readonly(com_vnode_t *node);
ino_t com_fs_vfs_new_ino(void);
