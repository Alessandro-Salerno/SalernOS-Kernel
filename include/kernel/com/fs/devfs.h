#pragma once

#include <kernel/com/fs/vfs.h>
#include <kernel/com/util.h>

typedef struct DevOps {
    VnodeType type; // CHARDEV or BLOCKDEV
    int (*read)(void *buf, usize buflen, void *devdata, u64 off, u64 flags,
                usize *out_bytes_read);
    int (*write)(void *devdata, void *buf, usize buflen, u64 off, u64 flags,
                 usize *out_bytes_written);
    int (*ioctl)(void *devdata, u64 op, void *buf);
    int (*isatty)(void *devdata);
    int (*seekable)(void *devdata);
    int (*stat)(void *devdata, struct stat *out_stat);
    int (*poll_head)(void *devdata, struct PollHead **out_poll_head);
    int (*poll)(void *devdata, short events, short *out_revents);
    int (*mmap)(void *devdata, VMMContext *vmm_context, void *hint, usize size,
                int vmm_flags, MMUFlags mmu_flags, u64 off, void **out_addr);

    int (*open)(void *devdata, Vnode **out_vnode);
    int (*close)(void *devdata);
    void (*inactive)(void *devdata);
} DevOps;

int devfs_register(Vnode *dir, const char *name, usize namelen, DevOps *devops,
                   void *devdata, Vnode **out_vnode);
int devfs_register_anonymous(DevOps *devops, void *devdata, Vnode **out_vnode);
int devfs_mkdir(Vnode *parent, const char *name, usize namelen,
                Vnode **out_vnode);
void *devfs_get_data(Vnode *node);
int devfs_init(Vfs *rootfs, Vfs **out_vfs);
