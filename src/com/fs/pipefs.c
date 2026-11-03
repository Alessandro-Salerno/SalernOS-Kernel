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

#include <arch/cpu.h>
#include <arch/info.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/pipefs.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <lib/ioviter.h>
#include <lib/sync.h>
#include <lib/util.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

#define PIPE_BUF_SZ ARCH_PAGE_SIZE

struct pipefs_node {
    uint8_t        *buf;
    size_t          read;
    size_t          write;
    ksync_t         condvar;
    com_waitlist_t  readers;
    com_waitlist_t  writers;
    com_vnode_t    *read_end;
    com_vnode_t    *write_end;
    com_poll_head_t pollhead;
};

static com_vnode_ops_t PipefsNodeOps = {.readv     = com_fs_pipefs_readv,
                                        .writev    = com_fs_pipefs_writev,
                                        .poll      = com_fs_pipefs_poll,
                                        .poll_head = com_fs_pipefs_poll_head,
                                        .close     = com_fs_pipefs_close,
                                        .stat      = com_fs_pipefs_stat,
                                        .seekable  = com_fs_pipefs_seekable};

static void pipefs_poll_notify(struct pipefs_node *pipe) {
    kspinlock_acquire(&pipe->pollhead.lock);
    com_polled_t *polled;
    LIST_FOREACH(polled, &pipe->pollhead.polled_list, polled_list) {
        com_fs_poll_notify(polled->poller);
    }
    kspinlock_release(&pipe->pollhead.lock);
}

int com_fs_pipefs_readv(kioviter_t  *ioviter,
                        size_t      *bytes_read,
                        com_vnode_t *node,
                        uintmax_t    off,
                        uintmax_t    flags) {
    (void)off;

    struct pipefs_node *pipe       = node->extra;
    size_t              buflen     = KIOVITER_REMAINING(ioviter);
    size_t              read_count = 0;
    int                 ret        = 0;
    ksync_acquire(&pipe->condvar);

    if (0 == buflen) {
        goto end;
    }

    while (pipe->write == pipe->read && NULL != pipe->write_end) {
        if (O_NONBLOCK & flags) {
            ret = EAGAIN;
            goto end;
        }
        ksync_wait(&pipe->condvar, &pipe->readers);
        if (COM_IPC_SIGNAL_NONE != com_ipc_signal_check()) {
            ret = EINTR;
            goto end;
        }
    }

    read_count = KMIN(buflen, pipe->write - pipe->read);
    if (0 == read_count) {
        goto end;
    }
    size_t modidx = pipe->read % PIPE_BUF_SZ;
    size_t first  = KMIN(read_count, PIPE_BUF_SZ - modidx);
    kioviter_write(NULL, ioviter, pipe->buf + modidx, first);
    if (read_count > first) {
        kioviter_write(NULL, ioviter, pipe->buf, read_count - first);
    }
    pipe->read += read_count;
    ksync_notify_all(&pipe->condvar, &pipe->writers);
    pipefs_poll_notify(pipe);

end:
    ksync_release(&pipe->condvar);
    *bytes_read = read_count;
    return ret;
}

int com_fs_pipefs_writev(size_t      *bytes_written,
                         com_vnode_t *node,
                         kioviter_t  *ioviter,
                         uintmax_t    off,
                         uintmax_t    flags) {
    (void)off;

    struct pipefs_node *pipe        = node->extra;
    size_t              buflen      = KIOVITER_REMAINING(ioviter);
    size_t              req_space   = buflen > PIPE_BUF_SZ ? 1 : buflen;
    size_t              write_count = 0;
    int                 ret         = 0;
    ksync_acquire(&pipe->condvar);

    for (size_t left = buflen; left > 0;) {
        size_t av_space = PIPE_BUF_SZ - (pipe->write - pipe->read);
        while (NULL != pipe->read_end && av_space < req_space) {
            if (O_NONBLOCK & flags) {
                ret = EAGAIN;
                goto end;
            }
            ksync_wait(&pipe->condvar, &pipe->writers);
            if (COM_IPC_SIGNAL_NONE != com_ipc_signal_check()) {
                ret = EINTR;
                goto end;
            }
            av_space = PIPE_BUF_SZ - (pipe->write - pipe->read);
        }
        if (NULL == pipe->read_end) {
            ret = EPIPE;
            goto end;
        }

        size_t writesz = KMIN(left, av_space);
        size_t modidx  = pipe->write % PIPE_BUF_SZ;
        size_t first   = KMIN(writesz, PIPE_BUF_SZ - modidx);
        kioviter_read(pipe->buf + modidx, first, NULL, ioviter);
        if (writesz > first) {
            kioviter_read(pipe->buf, writesz - first, NULL, ioviter);
        }
        pipe->write += writesz;
        left -= writesz;
        write_count += writesz;
        ksync_notify_all(&pipe->condvar, &pipe->readers);
        pipefs_poll_notify(pipe);
    }

end:
    ksync_release(&pipe->condvar);
    *bytes_written = write_count;
    if (EPIPE == ret) {
        com_thread_t *thread = ARCH_CPU_GET_THREAD();
        com_ipc_signal_send_to_thread(thread, SIGPIPE, thread->proc);
    }
    return write_count > 0 ? 0 : ret;
}

int com_fs_pipefs_poll(short *revents, com_vnode_t *node, short events) {
    struct pipefs_node *pipe = node->extra;
    short               out  = 0;
    ksync_acquire(&pipe->condvar);
    if (node == pipe->read_end) {
        if (pipe->write != pipe->read) {
            out |= events & POLLIN;
        }
        if (NULL == pipe->write_end) {
            out |= POLLHUP;
        }
    } else {
        if (pipe->write - pipe->read < PIPE_BUF_SZ) {
            out |= events & POLLOUT;
        }
        if (NULL == pipe->read_end) {
            out |= POLLERR;
        }
    }
    ksync_release(&pipe->condvar);
    *revents = out;
    return 0;
}

int com_fs_pipefs_poll_head(com_poll_head_t **out, com_vnode_t *node) {
    struct pipefs_node *pipe = node->extra;
    *out                     = &pipe->pollhead;
    return 0;
}

int com_fs_pipefs_close(com_vnode_t *vnode) {
    struct pipefs_node *pipe = vnode->extra;
    ksync_acquire(&pipe->condvar);
    if (vnode == pipe->read_end) {
        pipe->read_end = NULL;
        ksync_notify_all(&pipe->condvar, &pipe->writers);
    } else {
        pipe->write_end = NULL;
        ksync_notify_all(&pipe->condvar, &pipe->readers);
    }
    pipefs_poll_notify(pipe);
    bool last = NULL == pipe->read_end && NULL == pipe->write_end;
    ksync_release(&pipe->condvar);

    // Active I/O and poll registrations keep their endpoint referenced
    if (last) {
        com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(pipe->buf));
        com_mm_slab_free(pipe, sizeof(struct pipefs_node));
    }
    com_mm_slab_free(vnode, sizeof(com_vnode_t));
    return 0;
}

int com_fs_pipefs_stat(struct stat *out, com_vnode_t *node) {
    out->st_ino     = (ino_t)node->extra;
    out->st_mode    = S_IFIFO | 0600;
    out->st_nlink   = 1;
    out->st_blksize = PIPE_BUF_SZ;
    return 0;
}

int com_fs_pipefs_seekable(com_vnode_t *node) {
    (void)node;
    return ESPIPE;
}

void com_fs_pipefs_new(com_vnode_t **read, com_vnode_t **write) {
    struct pipefs_node *pipe = com_mm_slab_alloc(sizeof(struct pipefs_node));
    KSYNC_INIT_SPINLOCK(&pipe->condvar);
    pipe->pollhead.lock = KSPINLOCK_NEW();
    LIST_INIT(&pipe->pollhead.polled_list);
    COM_SYS_THREAD_WAITLIST_INIT(&pipe->readers);
    COM_SYS_THREAD_WAITLIST_INIT(&pipe->writers);
    pipe->read  = 0;
    pipe->write = 0;
    pipe->buf   = (uint8_t *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());

    com_vnode_t *r  = com_mm_slab_alloc(sizeof(com_vnode_t));
    r->extra        = pipe;
    r->mountpointof = NULL;
    r->ops          = &PipefsNodeOps;
    r->vfs          = NULL;
    r->num_ref      = 1;
    pipe->read_end  = r;

    com_vnode_t *w  = com_mm_slab_alloc(sizeof(com_vnode_t));
    w->extra        = pipe;
    w->mountpointof = NULL;
    w->ops          = &PipefsNodeOps;
    w->vfs          = NULL;
    w->num_ref      = 1;
    pipe->write_end = w;

    *read  = r;
    *write = w;
}
