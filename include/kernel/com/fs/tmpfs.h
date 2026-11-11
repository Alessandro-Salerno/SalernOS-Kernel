#pragma once

#include <kernel/com/fs/vfs.h>

typedef struct TmpFSSpecial {
    VnodeOps *ops;
    void *private;
} TmpFSSpecial;

void tmpfs_init(void);
int tmpfs_mount(Vnode *mountpoint, Vfs **out_vfs);

int tmpfs_new_orphan(Vfs *vfs, const VnodeAttr *attr, Vnode **out_vnode);

void *tmpfs_private(Vnode *vnode);
nlink_t tmpfs_nlink(Vnode *vnode);
void tmpfs_inactive(Vnode *vnode);
