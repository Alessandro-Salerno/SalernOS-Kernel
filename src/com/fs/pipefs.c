#include <arch/cpu.h>
#include <arch/info.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ds/ioviter.h>
#include <kernel/com/fs/pipefs.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sync/mutex.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <vendor/tailq.h>

#define PIPE_BUF_SZ PAGE_SIZE

struct PipeFSNode {
    u8 *buf;
    usize read;
    usize write;
    Mutex condvar;
    Waitlist readers;
    Waitlist writers;
    Vnode *read_end;
    Vnode *write_end;
    PollHead pollhead;
};

static int pipefs_ioctl(Vnode *node, u64 op, void *buf);

static VnodeOps g_pipefs_node_ops = {.readv = pipefs_readv,
                                     .writev = pipefs_writev,
                                     .poll = pipefs_poll,
                                     .poll_head = pipefs_poll_head,
                                     .inactive = pipefs_inactive,
                                     .stat = pipefs_stat,
                                     .seekable = pipefs_seekable,
                                     .ioctl = pipefs_ioctl};

static void pipefs_poll_notify(struct PipeFSNode *pipe) {
    spinlock_acq(&pipe->pollhead.lock);
    Polled *polled;
    LIST_FOREACH(polled, &pipe->pollhead.polled_list, polled_list) {
        poll_notify(polled->poller);
    }
    spinlock_rel(&pipe->pollhead.lock);
}

static int pipefs_ioctl(Vnode *node, u64 op, void *buf) {
    if (op != FIONREAD) return ENOSYS;

    struct PipeFSNode *pipe = node->extra;
    mutex_acq(&pipe->condvar);
    int avail = pipe->write - pipe->read;
    mutex_rel(&pipe->condvar);
    return memcpy_to_user(buf, &avail, sizeof(avail));
}

int pipefs_readv(IOVIter *ioviter, Vnode *node, u64 off, u64 flags,
                 usize *out_bytes_read) {
    (void)off;

    struct PipeFSNode *pipe = node->extra;
    usize buflen = IOVITER_REMAINING(ioviter);
    usize read_count = 0;
    int ret = 0;
    mutex_acq(&pipe->condvar);

    if (buflen == 0) goto end;

    while (pipe->write == pipe->read && pipe->write_end != NULL) {
        if (flags & O_NONBLOCK) {
            ret = EAGAIN;
            goto end;
        }
        mutex_wait(&pipe->condvar, &pipe->readers, 0, true);
        if (signal_check() != SIGNAL_NONE) {
            ret = EINTR;
            goto end;
        }
    }

    read_count = min(buflen, pipe->write - pipe->read);
    if (read_count == 0) goto end;
    usize modidx = pipe->read % PIPE_BUF_SZ;
    usize first = min(read_count, PIPE_BUF_SZ - modidx);
    usize done = 0;
    ret = ioviter_write(ioviter, pipe->buf + modidx, first, &done);
    if (ret == 0 && done == first && read_count > first) {
        usize second = 0;
        ret = ioviter_write(ioviter, pipe->buf, read_count - first, &second);
        done += second;
    }
    read_count = done;
    pipe->read += read_count;
    sched_notify_all(&pipe->writers);
    pipefs_poll_notify(pipe);

end:
    mutex_rel(&pipe->condvar);
    *out_bytes_read = read_count;
    return read_count > 0 ? 0 : ret;
}

int pipefs_writev(Vnode *node, IOVIter *ioviter, u64 off, u64 flags,
                  usize *out_bytes_written) {
    (void)off;

    struct PipeFSNode *pipe = node->extra;
    usize buflen = IOVITER_REMAINING(ioviter);
    usize req_space = buflen > PIPE_BUF_SZ ? 1 : buflen;
    usize write_count = 0;
    int ret = 0;
    mutex_acq(&pipe->condvar);

    for (usize left = buflen; left > 0;) {
        usize av_space = PIPE_BUF_SZ - (pipe->write - pipe->read);
        while (pipe->read_end != NULL && av_space < req_space) {
            if (flags & O_NONBLOCK) {
                ret = EAGAIN;
                goto end;
            }
            mutex_wait(&pipe->condvar, &pipe->writers, 0, true);
            if (signal_check() != SIGNAL_NONE) {
                ret = EINTR;
                goto end;
            }
            av_space = PIPE_BUF_SZ - (pipe->write - pipe->read);
        }
        if (pipe->read_end == NULL) {
            ret = EPIPE;
            goto end;
        }

        usize writesz = min(left, av_space);
        usize modidx = pipe->write % PIPE_BUF_SZ;
        usize first = min(writesz, PIPE_BUF_SZ - modidx);
        usize done = 0;
        ret = ioviter_read(pipe->buf + modidx, first, ioviter, &done);
        if (ret == 0 && done == first && writesz > first) {
            usize second = 0;
            ret = ioviter_read(pipe->buf, writesz - first, ioviter, &second);
            done += second;
        }
        pipe->write += done;
        left -= done;
        write_count += done;
        sched_notify_all(&pipe->readers);
        pipefs_poll_notify(pipe);
        if (done < writesz) {
            ret = EFAULT;
            goto end;
        }
    }

end:
    mutex_rel(&pipe->condvar);
    *out_bytes_written = write_count;
    if (ret == EPIPE) {
        Thread *thread = cpu_get_thread();
        signal_send_to_thread(thread, SIGPIPE, thread->proc);
    }
    return write_count > 0 ? 0 : ret;
}

int pipefs_poll(Vnode *node, short events, short *out_revents) {
    struct PipeFSNode *pipe = node->extra;
    short out = 0;
    mutex_acq(&pipe->condvar);
    if (node == pipe->read_end) {
        if (pipe->write != pipe->read) out |= events & POLLIN;
        if (pipe->write_end == NULL) out |= POLLHUP;
    } else {
        if (pipe->write - pipe->read < PIPE_BUF_SZ) out |= events & POLLOUT;
        if (pipe->read_end == NULL) out |= POLLERR;
    }
    mutex_rel(&pipe->condvar);
    *out_revents = out;
    return 0;
}

int pipefs_poll_head(Vnode *node, PollHead **out_poll_head) {
    struct PipeFSNode *pipe = node->extra;
    *out_poll_head = &pipe->pollhead;
    return 0;
}

// Pipe vnodes are only referenced by their files and operations in progress
void pipefs_inactive(Vnode *vnode) {
    struct PipeFSNode *pipe = vnode->extra;
    mutex_acq(&pipe->condvar);
    if (vnode == pipe->read_end) {
        pipe->read_end = NULL;
        sched_notify_all(&pipe->writers);
    } else {
        pipe->write_end = NULL;
        sched_notify_all(&pipe->readers);
    }
    pipefs_poll_notify(pipe);
    bool last = pipe->read_end == NULL && pipe->write_end == NULL;
    mutex_rel(&pipe->condvar);

    // Active I/O and poll registrations keep their endpoint referenced
    if (last) {
        pmm_free((void *)HHDM_TO_PHYS(pipe->buf));
        slab_free(pipe, sizeof(struct PipeFSNode));
    }
    slab_free(vnode, sizeof(Vnode));
}

int pipefs_stat(Vnode *node, struct stat *out_stat) {
    (void)node;
    out_stat->st_mode = 0600;
    out_stat->st_nlink = 1;
    out_stat->st_blksize = PIPE_BUF_SZ;
    return 0;
}

int pipefs_seekable(Vnode *node) {
    (void)node;
    return ESPIPE;
}

void pipefs_new(Vnode **out_read, Vnode **out_write) {
    struct PipeFSNode *pipe = slab_alloc(sizeof(struct PipeFSNode));
    mutex_init(&pipe->condvar);
    pipe->pollhead.lock = SPINLOCK_NEW();
    LIST_INIT(&pipe->pollhead.polled_list);
    waitlist_init(&pipe->readers);
    waitlist_init(&pipe->writers);
    pipe->read = 0;
    pipe->write = 0;
    pipe->buf = (u8 *)PHYS_TO_HHDM(pmm_alloc());

    Vnode *r = slab_alloc(sizeof(Vnode));
    vfs_vnode_init(r, NULL, VNODE_TYPE_FIFO, &g_pipefs_node_ops, pipe);
    pipe->read_end = r;

    Vnode *w = slab_alloc(sizeof(Vnode));
    vfs_vnode_init(w, NULL, VNODE_TYPE_FIFO, &g_pipefs_node_ops, pipe);
    w->ino = r->ino;
    pipe->write_end = w;

    *out_read = r;
    *out_write = w;
}
