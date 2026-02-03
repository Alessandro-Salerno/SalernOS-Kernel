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
#include <kernel/com/fs/pipefs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <lib/condvar.h>
#include <lib/mem.h>
#include <lib/util.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

#define PIPE_BUF_SZ ARCH_PAGE_SIZE

struct pipefs_node {
    uint8_t       *buf;
    size_t         read;
    size_t         write;
    kcondvar_t     condvar;
    com_waitlist_t readers;
    com_waitlist_t writers;
    com_vnode_t   *read_end;
    com_vnode_t   *write_end;
};

static com_vnode_ops_t PipefsNodeOps = {.read  = com_fs_pipefs_read,
                                        .write = com_fs_pipefs_write,
                                        .close = com_fs_pipefs_close};

int com_fs_pipefs_read(void        *buf,
                       size_t       buflen,
                       size_t      *bytes_read,
                       com_vnode_t *node,
                       uintmax_t    off,
                       uintmax_t    flags) {
    (void)off;
    (void)flags;

    struct pipefs_node *pipe = node->extra;
    kcondvar_acquire(&pipe->condvar);

    size_t read_count = 0;
    int    ret        = 0;

    for (size_t left = buflen; left > 0;) {
        while (pipe->write == pipe->read && NULL != pipe->write_end) {
            kcondvar_wait(&pipe->condvar, &pipe->readers);

            if (COM_IPC_SIGNAL_NONE != com_ipc_signal_check()) {
                if (0 == read_count) {
                    ret = EINTR;
                }
                goto end;
            }
        }

        size_t diff   = pipe->write - pipe->read;
        size_t readsz = KMIN(left, diff);
        size_t modidx = pipe->read % PIPE_BUF_SZ;
        size_t end    = PIPE_BUF_SZ - modidx;

        kmemcpy(buf, (uint8_t *)pipe->buf + modidx, KMIN(readsz, end));
        if (readsz > end) {
            kmemcpy((uint8_t *)buf + end, pipe->buf, readsz - end);
        }

        pipe->read += readsz;
        buf = (uint8_t *)buf + readsz;
        left -= readsz;
        read_count += readsz;

        // If the write end closed while we were reading
        if (NULL == pipe->write_end) {
            break;
        }

        kcondvar_notify(&pipe->condvar, &pipe->writers);
    }

end:
    kcondvar_release(&pipe->condvar);
    *bytes_read = read_count;
    if (NULL == pipe->read_end && NULL == pipe->write_end) {
        com_mm_slab_free(pipe, sizeof(struct pipefs_node));
        com_mm_slab_free(node, sizeof(com_vnode_t));
    }
    return ret;
}

int com_fs_pipefs_write(size_t      *bytes_written,
                        com_vnode_t *node,
                        void        *buf,
                        size_t       buflen,
                        uintmax_t    off,
                        uintmax_t    flags) {
    (void)off;
    (void)flags;

    struct pipefs_node *pipe = node->extra;
    kcondvar_acquire(&pipe->condvar);

    size_t req_space   = buflen;
    size_t write_count = 0;

    if (buflen > PIPE_BUF_SZ) {
        req_space = 1;
    }

    int ret = 0;

    for (size_t left = buflen; left > 0;) {
        size_t av_space = PIPE_BUF_SZ - (pipe->write - pipe->read);

        while (av_space < req_space) {
            kcondvar_wait(&pipe->condvar, &pipe->writers);

            if (COM_IPC_SIGNAL_NONE != com_ipc_signal_check()) {
                if (0 == write_count) {
                    ret = EINTR;
                }
                goto end;
            }
            av_space = PIPE_BUF_SZ - (pipe->write - pipe->read);
        }

        size_t writesz = KMIN(left, av_space);
        size_t modidx  = pipe->write % PIPE_BUF_SZ;
        size_t end     = PIPE_BUF_SZ - modidx;

        kmemcpy(pipe->buf + modidx, buf, KMIN(writesz, end));
        if (writesz > end) {
            kmemcpy(pipe->buf, (uint8_t *)buf + end, writesz - end);
        }

        pipe->write += writesz;
        buf = (uint8_t *)buf + writesz;
        left -= writesz;
        write_count += writesz;

        kcondvar_notify(&pipe->condvar, &pipe->readers);
    }

end:
    kcondvar_release(&pipe->condvar);
    *bytes_written = write_count;
    if (NULL == pipe->read_end && NULL == pipe->write_end) {
        com_mm_slab_free(pipe, sizeof(struct pipefs_node));
        com_mm_slab_free(node, sizeof(com_vnode_t));
    }

    return ret;
}

int com_fs_pipefs_close(com_vnode_t *vnode) {
    struct pipefs_node *pipe         = vnode->extra;
    bool                has_notified = false;
    kcondvar_acquire(&pipe->condvar);

    if (vnode == pipe->read_end) {
        has_notified   = !COM_SYS_THREAD_WAITLIST_EMPTY(&pipe->writers);
        pipe->read_end = NULL;
        kcondvar_notify(&pipe->condvar, &pipe->writers);
    } else if (vnode == pipe->write_end) {
        has_notified    = !COM_SYS_THREAD_WAITLIST_EMPTY(&pipe->readers);
        pipe->write_end = NULL;
        kcondvar_notify(&pipe->condvar, &pipe->readers);
    }

    kcondvar_release(&pipe->condvar);
    if (!has_notified && NULL == pipe->write_end && NULL == pipe->read_end) {
        com_mm_slab_free(pipe, sizeof(struct pipefs_node));
        com_mm_slab_free(vnode, sizeof(com_vnode_t));
    }

    return 0;
}

void com_fs_pipefs_new(com_vnode_t **read, com_vnode_t **write) {
    struct pipefs_node *pipe = com_mm_slab_alloc(sizeof(struct pipefs_node));
    KCONDVAR_INIT_SPINLOCK(&pipe->condvar);
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
