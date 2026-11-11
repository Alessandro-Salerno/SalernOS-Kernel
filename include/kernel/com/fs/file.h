#pragma once

#include <kernel/com/sync/spinlock.h>
#include <kernel/com/util.h>

// NOTE: before the includes, which may include this header again through
// <kernel/com/sys/proc.h>
struct Vnode;

typedef struct File {
    Spinlock off_lock;
    u64 off;
    u64 flags;
    u64 num_ref;
    struct Vnode *vnode;
} File;

typedef struct FileDesc {
    File *file;
    u64 flags;
    bool reserved; // see proc_next_fd
} FileDesc;

#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/slab.h>

#define FILE_HOLD(file)                                          \
    if ((file) != NULL) {                                        \
        __atomic_add_fetch(&file->num_ref, 1, __ATOMIC_RELAXED); \
    }

#define FILE_REL(file)                                                        \
    ({                                                                        \
        if (file != NULL && file->vnode != NULL) {                            \
            u64 n = __atomic_add_fetch(&file->num_ref, -1, __ATOMIC_ACQ_REL); \
            if (n == 0) {                                                     \
                log_debug("freeing file %p", (file));                         \
                vfs_close(file->vnode);                                       \
                vfs_vnode_rel(file->vnode);                                   \
                slab_free(file, sizeof(File));                                \
                file = NULL;                                                  \
            }                                                                 \
        }                                                                     \
    })
