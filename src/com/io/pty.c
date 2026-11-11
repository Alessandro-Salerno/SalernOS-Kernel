#include <asm/ioctls.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/ds/ringbuffer.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/io/pty.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <sys/ttydefaults.h>
#include <vendor/tailq.h>

static int g_next_pts_num = 0;
static Vnode *g_pts_dir = NULL;

// C FORWARD DELCARATION PROTOTYPES
static void ptm_poll_callback(void *arg);
static DevOps g_pts_dev_ops;
static DevOps g_pts_name_dev_ops;

// GENERIC Pty FUNCTIONS

static int pty_rb_check_hangup(usize curr_nbytes, Ringbuffer *rb, int op,
                               void *arg, usize *out_new_nbytes,
                               bool *out_force_return) {
    (void)curr_nbytes;
    (void)rb;
    Pty *pty = arg;
    assert(pty != NULL);

    if (op == RINGBUFFER_OP_WRITE) {
        if (__atomic_load_n(&pty->num_slaves, __ATOMIC_SEQ_CST) == 0 ||
            atomic_load(&pty->master_vn) == NULL) {
            *out_new_nbytes = 0;
            return EIO;
        }
    } else if (op == RINGBUFFER_OP_READ) {
        if (__atomic_load_n(&pty->num_slaves, __ATOMIC_SEQ_CST) == 0) {
            *out_new_nbytes = 0;
            return EIO;
        } else if (atomic_load(&pty->master_vn) == NULL) {
            *out_new_nbytes = 0;
            *out_force_return = true;
        }
    }

    return 0;
}

static int pty_echo(const char *buf, usize buflen, bool blocking,
                    void *passthrough, usize *out_bytes_written) {
    assert(!MMU_RANGE_IS_USER(buf, buflen));
    Pty *pty = passthrough;
    return ringbuffer_write(&pty->master_rb, (void *)buf, buflen,
                            RINGBUFFER_NOATOMIC, blocking, ptm_poll_callback,
                            pty, NULL, out_bytes_written);
}

static Pty *pty_alloc(void) {
    // pty initialization
    Pty *pty = slab_alloc(sizeof(Pty));
    pty->pts_num = __atomic_fetch_add(&g_next_pts_num, 1, __ATOMIC_SEQ_CST);
    tty_init_text_backend(&pty->backend, 80, 25, pty_echo);

    // ringbuffer initializations (slave is done by tty)
    ringbuffer_init(&pty->master_rb);
    pty->master_rb.check_hangup = pty_rb_check_hangup;
    pty->master_rb.fallback_hu_arg = pty;
    pty->backend.slave_rb.check_hangup = pty_rb_check_hangup;
    pty->backend.slave_rb.fallback_hu_arg = pty;

    // poll initialization
    LIST_INIT(&pty->master_ph.polled_list);
    pty->master_ph.lock = SPINLOCK_NEW();
    pty->num_ref = 1;
    return pty;
}

static void pty_rel(Pty *pty) {
    if (__atomic_add_fetch(&pty->num_ref, -1, __ATOMIC_ACQ_REL) == 0) {
        slab_free(pty, sizeof(Pty));
    }
}

// PTM FUNCTIONS

static void ptm_poll_callback(void *arg) {
    Pty *pty = arg;

    spinlock_acq(&pty->master_ph.lock);
    Polled *polled, *_;
    LIST_FOREACH_SAFE(polled, &pty->master_ph.polled_list, polled_list, _) {
        poll_notify(polled->poller);
    }
    spinlock_rel(&pty->master_ph.lock);
}

static int ptm_read(void *buf, usize buflen, void *devdata, u64 off, u64 flags,
                    usize *out_bytes_read) {
    (void)off;
    // master_rb is a spinlock, so never copy from it into buf. Only the first
    // chunk may block, then return whatever was already available
    Pty *pty = devdata;
    usize done = 0;
    int ret = 0;
    char chunk[128];
    while (done < buflen) {
        usize n = min(buflen - done, sizeof(chunk));
        usize r = 0;
        ret = ringbuffer_read(chunk, &pty->master_rb, n, RINGBUFFER_NOATOMIC,
                              done == 0 && !(flags & O_NONBLOCK),
                              ptm_poll_callback, pty, NULL, &r);
        // The chunk is lost if it cannot be delivered
        int copy_ret = memcpy_maybe_user((char *)buf + done, chunk, r);
        if (copy_ret != 0) {
            ret = copy_ret;
            break;
        }
        done += r;
        if (ret != 0 || r < n) break;
    }
    *out_bytes_read = done;
    return done != 0 ? 0 : ret;
}

static int ptm_write(void *devdata, void *buf, usize buflen, u64 off, u64 flags,
                     usize *out_bytes_written) {
    (void)off;
    (void)flags;
    Pty *pty = devdata;
    return tty_process_chars(&pty->backend, buf, buflen, true, pty,
                             out_bytes_written);
}

// NOTE: this is different from ioctl on a tty or pts because ptms are not
// properly ttys
static int ptm_ioctl(void *devdata, u64 op, void *buf) {
    Pty *pty = devdata;

    if (op == TIOCSPTLCK) return 0;

    if (op == TIOCGPTN) {
        int num = pty->pts_num;
        return memcpy_to_user(buf, &num, sizeof(num));
    }

    if (op == TIOCSWINSZ) {
        struct winsize ws;
        int ret = memcpy_from_user(&ws, buf, sizeof(ws));
        if (ret == 0) {
            pty->backend.cols = ws.ws_col;
            pty->backend.rows = ws.ws_row;
        }
        return ret;
    }

    if (op == TIOCGWINSZ) {
        struct winsize ws = {.ws_row = pty->backend.rows,
                             .ws_col = pty->backend.cols};
        return memcpy_to_user(buf, &ws, sizeof(ws));
    }

    if (op == TCGETS) {
        return memcpy_to_user(buf, &pty->backend.termios,
                              sizeof(pty->backend.termios));
    }

    return ENOSYS;
}

static int ptm_isatty(void *devdata) {
    (void)devdata;
    return 0;
}

static int ptm_seekable(void *devdata) {
    (void)devdata;
    return ESPIPE;
}

static int ptm_close(void *devdata) {
    Pty *pty = devdata;
    atomic_store(&pty->master_vn, NULL);

    ringbuffer_wake_all(&pty->backend.slave_rb);
    ringbuffer_wake_all(&pty->master_rb);
    tty_text_backend_poll_callback(&pty->backend);

    // New opens fail from now on, see pts_open
    char pts_name[24];
    uitoa(pty->pts_num, pts_name);
    vfs_unlink(g_pts_dir, pts_name, strlen(pts_name), 0);
    return 0;
}

static void ptm_inactive(void *devdata) { pty_rel(devdata); }

static int ptm_poll_head(void *devdata, PollHead **out_poll_head) {
    Pty *pty = devdata;
    *out_poll_head = &pty->master_ph;
    return 0;
}

static int ptm_poll(void *devdata, short events, short *out_revents) {
    Pty *pty = devdata;
    (void)events;

    short out = ringbuffer_poll(&pty->master_rb);
    if (__atomic_load_n(&pty->num_slaves, __ATOMIC_RELAXED) == 0) {
        out |= POLLHUP;
    }

    *out_revents = out;
    return 0;
}

static int ptm_stat(void *devdata, struct stat *out_stat) {
    (void)devdata;
    out_stat->st_mode = 0600;
    return 0;
}

static DevOps g_ptm_dev_ops = {.type = VNODE_TYPE_CHARDEV,
                               .read = ptm_read,
                               .write = ptm_write,
                               .ioctl = ptm_ioctl,
                               .isatty = ptm_isatty,
                               .seekable = ptm_seekable,
                               .poll_head = ptm_poll_head,
                               .poll = ptm_poll,
                               .stat = ptm_stat,
                               .close = ptm_close,
                               .inactive = ptm_inactive};

// PTS FUNCTIONS

static int pts_read(void *buf, usize buflen, void *devdata, u64 off, u64 flags,
                    usize *out_bytes_read) {
    (void)off;
    PTYSlave *pty_slave = devdata;
    Pty *pty = pty_slave->pty;
    return ringbuffer_read(buf, &pty->backend.slave_rb, buflen,
                           RINGBUFFER_NOATOMIC, !(flags & O_NONBLOCK),
                           tty_text_backend_poll_callback, &pty->backend, NULL,
                           out_bytes_read);
}

static int pts_write(void *devdata, void *buf, usize buflen, u64 off, u64 flags,
                     usize *out_bytes_written) {
    (void)off;
    (void)flags;
    PTYSlave *pty_slave = devdata;
    Pty *pty = pty_slave->pty;
    return tty_text_backend_echo(&pty->backend, buf, buflen, true, pty,
                                 out_bytes_written);
}

static int pts_ioctl(void *devdata, u64 op, void *buf) {
    PTYSlave *pty_slave = devdata;
    Pty *pty = pty_slave->pty;

    return tty_text_backend_ioctl(&pty->backend, pty_slave->vnode, op, buf);
}

static int pts_isatty(void *devdata) {
    (void)devdata;
    return 0;
}

static int pts_seekable(void *devdata) {
    (void)devdata;
    return ESPIPE;
}

// num_slaves counts open files, not references, so that the master sees a
// hangup even if a session still has the slave as its controlling terminal
static int pts_close(void *devdata) {
    PTYSlave *pty_slave = devdata;
    Pty *pty = pty_slave->pty;

    if (__atomic_add_fetch(&pty->num_slaves, -1, __ATOMIC_SEQ_CST) == 0) {
        ringbuffer_wake_all(&pty->master_rb);
        ptm_poll_callback(pty);
    }

    return 0;
}

static void pts_inactive(void *devdata) {
    PTYSlave *pty_slave = devdata;
    pty_rel(pty_slave->pty);
    slab_free(pty_slave, sizeof(PTYSlave));
}

// Opens a slave that is already open (e.g., through /dev/tty)
static int pts_reopen(void *devdata, Vnode **out_vnode) {
    PTYSlave *pty_slave = devdata;
    __atomic_add_fetch(&pty_slave->pty->num_slaves, 1, __ATOMIC_SEQ_CST);
    vfs_vnode_hold(pty_slave->vnode);
    *out_vnode = pty_slave->vnode;
    return 0;
}

static int pts_stat(void *devdata, struct stat *out_stat) {
    (void)devdata;
    out_stat->st_blksize = 0;
    out_stat->st_mode = 0622; // apparently makes xterm happy
    return 0;
}

static int pts_poll_head(void *devdata, PollHead **out_poll_head) {
    PTYSlave *pty_slave = devdata;
    Pty *pty = pty_slave->pty;
    *out_poll_head = &pty->backend.slave_ph;
    return 0;
}

static int pts_poll(void *devdata, short events, short *out_revents) {
    (void)events;
    PTYSlave *pty_slave = devdata;
    Pty *pty = pty_slave->pty;

    short out = ringbuffer_poll(&pty->backend.slave_rb);
    if (atomic_load(&pty->master_vn) == NULL) out |= POLLHUP;

    *out_revents = out;
    return 0;
}

// Opens /dev/pts/N: every open gets its own slave vnode
static int pts_open(void *devdata, Vnode **out_vnode) {
    Pty *pty = devdata;
    // A lookup may have found the node just before the master closed
    if (atomic_load(&pty->master_vn) == NULL) return EIO;

    PTYSlave *pty_slave = slab_alloc(sizeof(PTYSlave));
    pty_slave->pty = pty;

    int ret = devfs_register_anonymous(&g_pts_dev_ops, pty_slave,
                                       &pty_slave->vnode);
    if (ret != 0) {
        slab_free(pty_slave, sizeof(PTYSlave));
        return ret;
    }

    __atomic_add_fetch(&pty->num_ref, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&pty->num_slaves, 1, __ATOMIC_SEQ_CST);
    *out_vnode = pty_slave->vnode;
    return 0;
}

static void pts_name_inactive(void *devdata) { pty_rel(devdata); }

static DevOps g_pts_dev_ops = {.type = VNODE_TYPE_CHARDEV,
                               .read = pts_read,
                               .write = pts_write,
                               .ioctl = pts_ioctl,
                               .isatty = pts_isatty,
                               .seekable = pts_seekable,
                               .stat = pts_stat,
                               .poll_head = pts_poll_head,
                               .poll = pts_poll,
                               .open = pts_reopen,
                               .close = pts_close,
                               .inactive = pts_inactive};

static DevOps g_pts_name_dev_ops = {.type = VNODE_TYPE_CHARDEV,
                                    .open = pts_open,
                                    .stat = pts_stat,
                                    .inactive = pts_name_inactive};

static int ptmx_open(void *devdata, Vnode **out_vnode) {
    (void)devdata;

    Pty *pty = pty_alloc();

    // The master comes first because pts_open needs it
    Vnode *master_vn = NULL;
    int ret = devfs_register_anonymous(&g_ptm_dev_ops, pty, &master_vn);
    if (ret != 0) {
        pty_rel(pty);
        return ret;
    }
    atomic_store(&pty->master_vn, master_vn);

    char pts_name[24];
    uitoa(pty->pts_num, pts_name);

    // This *ONLY* creates the pts device, it does NOT open it
    __atomic_add_fetch(&pty->num_ref, 1, __ATOMIC_RELAXED);
    ret = devfs_register(g_pts_dir, pts_name, strlen(pts_name),
                         &g_pts_name_dev_ops, pty, NULL);
    if (ret != 0) {
        pty_rel(pty);
        vfs_vnode_rel(master_vn);
        return ret;
    }

    *out_vnode = master_vn;
    return 0;
}

static int ptmx_stat(void *devdata, struct stat *out_stat) {
    (void)devdata;
    out_stat->st_blksize = 0;
    out_stat->st_mode = 0622; // apparently makes xterm happy
    return 0;
}

static DevOps g_clone_dev_ops = {.type = VNODE_TYPE_CHARDEV,
                                 .open = ptmx_open,
                                 .stat = ptmx_stat};

int pty_init(void) {
    log("initializing pty subsytem");

    int ret = devfs_mkdir(NULL, "pts", 3, &g_pts_dir);
    if (ret != 0) goto end;

    ret = devfs_register(NULL, "ptmx", 4, &g_clone_dev_ops, NULL, NULL);
    if (ret != 0) goto end;

end:
    return ret;
}
