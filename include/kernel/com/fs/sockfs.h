#pragma once

#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/socket/socket.h>
#include <kernel/com/util.h>

typedef struct SocketVnode {
    Vnode vnode;
    Socket *socket;
} SocketVnode;

int sockfs_readv(IOVIter *ioviter, Vnode *node, u64 off, u64 flags,
                 usize *out_bytes_read);
int sockfs_writev(Vnode *node, IOVIter *ioviter, u64 off, u64 flags,
                  usize *out_bytes_written);

int sockfs_ioctl(Vnode *node, u64 op, void *buf);
int sockfs_poll_head(Vnode *node, struct PollHead **out_poll_head);
int sockfs_poll(Vnode *node, short events, short *out_revents);
void sockfs_inactive(Vnode *node);
int sockfs_seekable(Vnode *node);

// OTHER FUNCTIONS

Vnode *sockfs_new(void);
