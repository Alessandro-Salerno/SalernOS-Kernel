#pragma once

#include <arch/info.h>
#include <kernel/com/ds/ringbuffer.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <stdatomic.h>
#include <termios.h>

typedef struct Pty {
    TextTTYBackend backend;

    _Atomic(Vnode *) master_vn;
    Ringbuffer master_rb;
    PollHead master_ph;

    // no slave_rb/ph because they're in backend
    // no slave_vn because there may be multiple slaves (?)

    int pts_num;
    usize num_slaves;
    usize num_ref; // master vnode, /dev/pts/N, and slave vnodes
} Pty;

typedef struct PTYSlave {
    Vnode *vnode;
    Pty *pty;
} PTYSlave;

int pty_init(void);
