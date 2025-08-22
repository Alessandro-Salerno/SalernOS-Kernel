/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
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

#include <asm/ioctls.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/io/pty.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/sched.h>
#include <lib/ringbuffer.h>
#include <lib/str.h>
#include <lib/util.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <sys/ttydefaults.h>
#include <vendor/tailq.h>

static int          NextPtsNum = 0;
static com_vnode_t *PtsDir     = NULL;

// C FORWARD DELCARATION PROTOTYPES
static void          ptm_poll_callback(void *arg);
static com_dev_ops_t PtsDevOps;

// GENERIC PTY FUNCTIONS

static int pty_rb_check_hangup(size_t        *new_nbytes,
                               size_t         curr_nbytes,
                               bool          *force_return,
                               kringbuffer_t *rb,
                               int            op,
                               void          *arg) {
    (void)rb;
    com_pty_t *pty = arg;

    if (KRINGBUFFER_OP_WRITE == op) {
        if (0 == __atomic_load_n(&pty->num_slaves, __ATOMIC_SEQ_CST)) {
            *new_nbytes = 0;
            return EIO;
        }
    } else if (KRINGBUFFER_OP_READ == op) {
        if (0 == __atomic_load_n(&pty->num_slaves, __ATOMIC_SEQ_CST)) {
            *new_nbytes = 0;
            return EIO;
        } else if (NULL == atomic_load(&pty->master_vn)) {
            *new_nbytes   = 0;
            *force_return = true;
        }
    }

    return 0;
}

static int pty_echo(size_t     *bytes_written,
                    const char *buf,
                    size_t      buflen,
                    bool        blocking,
                    void       *passthrough) {
    com_pty_t *pty = passthrough;
    KDEBUG("PTM ECHO %.*s TO PTY %x", buflen, buf, pty);
    return kringbuffer_write(bytes_written,
                             &pty->master_rb,
                             (void *)buf,
                             buflen,
                             blocking,
                             ptm_poll_callback,
                             pty,
                             pty);
}

static com_pty_t *pty_alloc(void) {
    com_pty_t *pty = com_mm_slab_alloc(sizeof(com_pty_t));
    pty->pts_num   = __atomic_fetch_add(&NextPtsNum, 1, __ATOMIC_SEQ_CST);
    com_io_tty_init_text_backend(&pty->backend, 80, 25, pty_echo);
    KRINGBUFFER_INIT(&pty->master_rb);
    pty->master_rb.check_hangup        = pty_rb_check_hangup;
    pty->backend.slave_rb.check_hangup = pty_rb_check_hangup;
    LIST_INIT(&pty->master_ph.polled_list);
    pty->master_ph.lock = COM_SPINLOCK_NEW();
    return pty;
}

// PTM FUNCTIONS

static void ptm_poll_callback(void *arg) {
    com_pty_t *pty = arg;
    KDEBUG("PTM POLL CALLBACK ON PTY %x", pty);

    com_spinlock_acquire(&pty->master_ph.lock);
    com_polled_t *polled, *_;
    LIST_FOREACH_SAFE(polled, &pty->master_ph.polled_list, polled_list, _) {
        KDEBUG("notifying polled %x", polled);
        com_spinlock_acquire(&polled->poller->lock);
        com_sys_sched_notify(&polled->poller->waiters);
        com_spinlock_release(&polled->poller->lock);
    }
    com_spinlock_release(&pty->master_ph.lock);
}

static int ptm_read(void     *buf,
                    size_t    buflen,
                    size_t   *bytes_read,
                    void     *devdata,
                    uintmax_t off,
                    uintmax_t flags) {
    (void)off;
    com_pty_t *pty = devdata;
    int        ret = 0;
    KDEBUG("PTM READ");
    com_spinlock_acquire(&pty->master_rb.lock);

    if (0 == __atomic_load_n(&pty->num_slaves, __ATOMIC_SEQ_CST)) {
        ret = EIO;
        goto end;
    }

    ret = kringbuffer_read_nolock(buf,
                                  bytes_read,
                                  &pty->master_rb,
                                  buflen,
                                  false,
                                  ptm_poll_callback,
                                  pty,
                                  pty);

end:
    com_spinlock_release(&pty->master_rb.lock);
    KDEBUG("PTM READ: %.*s", *bytes_read, buf);
    return ret;
}

static int ptm_write(size_t   *bytes_written,
                     void     *devdata,
                     void     *buf,
                     size_t    buflen,
                     uintmax_t off,
                     uintmax_t flags) {
    (void)off;
    (void)flags;
    com_pty_t *pty = devdata;
    return com_io_tty_process_chars(
        bytes_written, &pty->backend, buf, buflen, true, pty);
}

static int ptm_ioctl(void *devdata, uintmax_t op, void *buf) {
    com_pty_t *pty = devdata;

    if (TIOCSPTLCK == op) {
        return 0;
    }

    if (TIOCGPTN == op) {
        *(int *)buf = pty->pts_num;
        return 0;
    }

    if (TIOCSWINSZ == op) {
        struct winsize *ws = (void *)buf;
        pty->backend.cols  = ws->ws_col;
        pty->backend.rows  = ws->ws_row;
        return 0;
    }

    if (TIOCGWINSZ == op) {
        struct winsize *ws = (void *)buf;
        ws->ws_col         = pty->backend.cols;
        ws->ws_row         = pty->backend.rows;
        return 0;
    }

    if (TCGETS == op) {
        struct termios *termios = buf;
        *termios                = pty->backend.termios;
        return 0;
    }

    return ENOSYS;
}

static int ptm_isatty(void *devdata) {
    (void)devdata;
    return 0;
}

static int ptm_close(void *devdata) {
    com_pty_t *pty = devdata;
    atomic_store(&pty->master_vn, NULL);
    return 0;
}

static int ptm_poll_head(com_poll_head_t **out, void *devdata) {
    com_pty_t *pty = devdata;
    *out           = &pty->master_ph;
    KDEBUG("PTM POLL HEAD");
    return 0;
}

static int ptm_poll(short *revents, void *devdata, short events) {
    com_pty_t *pty = devdata;
    (void)events;
    KDEBUG("PTM POLL CHECK");

    short out = 0;
    if (0 == pty->num_slaves) {
        out |= POLLHUP;
    }
    if (pty->master_rb.write.index != pty->master_rb.read.index) {
        KDEBUG("PTM POLLIN %u != %u",
               pty->master_rb.write.index,
               pty->master_rb.read.index);
        out |= POLLIN;
    }
    if (KRINGBUFFER_SIZE -
            (pty->master_rb.write.index - pty->master_rb.read.index) >
        0) {
        KDEBUG("PTM POLLOUT %u",
               KRINGBUFFER_SIZE -
                   (pty->master_rb.write.index - pty->master_rb.read.index))
        out |= POLLOUT;
    }

    *revents = out;
    return 0;
}

static com_dev_ops_t PtmDevOps = {.read      = ptm_read,
                                  .write     = ptm_write,
                                  .ioctl     = ptm_ioctl,
                                  .isatty    = ptm_isatty,
                                  .poll_head = ptm_poll_head,
                                  .poll      = ptm_poll,
                                  .close     = ptm_close};

// PTS FUNCTIONS

static int pts_read(void     *buf,
                    size_t    buflen,
                    size_t   *bytes_read,
                    void     *devdata,
                    uintmax_t off,
                    uintmax_t flags) {
    (void)off;
    com_pty_slave_t *pty_slave = devdata;
    com_pty_t       *pty       = pty_slave->pty;
    KDEBUG("PTS READ");
    int ret = kringbuffer_read(buf,
                               bytes_read,
                               &pty->backend.slave_rb,
                               buflen,
                               !(O_NONBLOCK & flags),
                               com_io_tty_text_backend_poll_callback,
                               &pty->backend,
                               pty);
    KDEBUG("PTS READ: %.*s", *bytes_read, buf);
    return ret;
}

static int pts_write(size_t   *bytes_written,
                     void     *devdata,
                     void     *buf,
                     size_t    buflen,
                     uintmax_t off,
                     uintmax_t flags) {
    (void)off;
    (void)flags;
    com_pty_slave_t *pty_slave = devdata;
    com_pty_t       *pty       = pty_slave->pty;
    KDEBUG("PTS WRITE TO PTY %x", pty);
    return pty_echo(bytes_written, buf, buflen, true, pty);
}

static int pts_ioctl(void *devdata, uintmax_t op, void *buf) {
    com_pty_slave_t *pty_slave = devdata;
    com_pty_t       *pty       = pty_slave->pty;

    if (TIOCSWINSZ == op) {
        struct winsize *ws = (void *)buf;
        pty->backend.cols  = ws->ws_col;
        pty->backend.rows  = ws->ws_row;
        return 0;
    }

    return com_io_tty_text_backend_ioctl(
        &pty->backend, pty_slave->vnode, op, buf);
}

static int pts_isatty(void *devdata) {
    (void)devdata;
    return 0;
}

static int pts_close(void *devdata) {
    com_pty_slave_t *pty_slave = devdata;
    com_pty_t       *pty       = pty_slave->pty;

    if (0 == __atomic_add_fetch(&pty->num_slaves, -1, __ATOMIC_SEQ_CST)) {
        com_spinlock_acquire(&pty->master_rb.lock);
        com_sys_sched_notify_all(&pty->master_rb.read.queue);
        com_sys_sched_notify_all(&pty->master_rb.write.queue);
        com_spinlock_release(&pty->master_rb.lock);
        ptm_poll_callback(pty);
    }

    com_mm_slab_free(pty_slave, sizeof(com_pty_slave_t));
    return 0;
}

static int pts_stat(struct stat *out, void *devdata) {
    out->st_blksize = 0;
    out->st_ino     = (ino_t)devdata;
    out->st_mode    = 0622; // apparently makes xterm happy
    out->st_mode |= S_IFCHR;
    return 0;
}

static int pts_poll_head(com_poll_head_t **out, void *devdata) {
    com_pty_slave_t *pty_slave = devdata;
    com_pty_t       *pty       = pty_slave->pty;
    *out                       = &pty->backend.slave_ph;
    KDEBUG("PTS POLL HEAD");
    return 0;
}

static int pts_poll(short *revents, void *devdata, short events) {
    (void)events;
    com_pty_slave_t *pty_slave = devdata;
    com_pty_t       *pty       = pty_slave->pty;
    KDEBUG("PTS POLL CHECK");

    short out = 0;

    if (pty->backend.slave_rb.write.index != pty->backend.slave_rb.read.index) {
        out |= POLLIN;
    }
    if (KRINGBUFFER_SIZE - (pty->backend.slave_rb.write.index -
                            pty->backend.slave_rb.read.index) >
        0) {
        out |= POLLOUT;
    }

    *revents = out;
    return 0;
}

static int pts_open(com_vnode_t **out, void *devdata) {
    com_pty_slave_t *pty_slave = com_mm_slab_alloc(sizeof(com_pty_slave_t));
    com_pty_t       *pty       = devdata;
    __atomic_add_fetch(&pty->num_slaves, 1, __ATOMIC_SEQ_CST);
    pty_slave->pty = pty;

    int ret = com_fs_devfs_register_anonymous(
        &pty_slave->vnode, &PtsDevOps, pty_slave);
    if (0 != ret) {
        com_mm_slab_free(pty_slave, sizeof(com_pty_slave_t));
        __atomic_add_fetch(&pty->num_slaves, -1, __ATOMIC_SEQ_CST);
        KASSERT(false);
        goto end;
    }

    *out = pty_slave->vnode;
end:
    return ret;
}

static com_dev_ops_t PtsDevOps = {.read      = pts_read,
                                  .write     = pts_write,
                                  .ioctl     = pts_ioctl,
                                  .isatty    = pts_isatty,
                                  .stat      = pts_stat,
                                  .poll_head = pts_poll_head,
                                  .poll      = pts_poll,
                                  .close     = pts_close,
                                  .open      = pts_open};

static int ptmx_open(com_vnode_t **out, void *devdata) {
    (void)devdata;

    com_pty_t *pty = pty_alloc();
    char       pts_name[24];
    kuitoa(pty->pts_num, pts_name);

    // This *ONLY* creates the pts device, it does NOT open it
    com_vnode_t *slave_vn;
    int          ret = com_fs_devfs_register(
        &slave_vn, PtsDir, pts_name, kstrlen(pts_name), &PtsDevOps, pty);
    if (0 != ret) {
        goto end;
    }

    com_vnode_t *master_vn = NULL;
    ret = com_fs_devfs_register_anonymous(&master_vn, &PtmDevOps, pty);
    if (0 != ret) {
        goto end;
    }

    atomic_store(&pty->master_vn, master_vn);
    *out = master_vn;

end:
    return ret;
}

static int ptmx_stat(struct stat *out, void *devdata) {
    out->st_blksize = 0;
    out->st_ino     = (ino_t)devdata;
    out->st_mode    = 0622; // apparently makes xterm happy
    out->st_mode |= S_IFCHR;
    return 0;
}

static com_dev_ops_t CloneDevOps = {.open = ptmx_open, .stat = ptmx_stat};

int com_io_pty_init(void) {
    KLOG("initializing pty subsytem");

    int ret = com_fs_devfs_mkdir(&PtsDir, NULL, "pts", 3, 0);
    if (0 != ret) {
        goto end;
    }

    com_vnode_t *ptmx_dev = NULL;
    ret = com_fs_devfs_register(&ptmx_dev, NULL, "ptmx", 4, &CloneDevOps, NULL);
    if (0 != ret) {
        goto end;
    }

end:
    return ret;
}
