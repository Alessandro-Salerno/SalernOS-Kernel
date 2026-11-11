#pragma once

#include <kernel/com/fs/vfs.h>
#include <kernel/com/util.h>

int pipefs_readv(IOVIter *ioviter, Vnode *node, u64 off, u64 flags,
                 usize *out_bytes_read);
int pipefs_writev(Vnode *node, IOVIter *ioviter, u64 off, u64 flags,
                  usize *out_bytes_written);
int pipefs_poll(Vnode *node, short events, short *out_revents);
int pipefs_poll_head(Vnode *node, struct PollHead **out_poll_head);
void pipefs_inactive(Vnode *vnode);
int pipefs_stat(Vnode *node, struct stat *out_stat);
int pipefs_seekable(Vnode *node);

void pipefs_new(Vnode **out_read, Vnode **out_write);
