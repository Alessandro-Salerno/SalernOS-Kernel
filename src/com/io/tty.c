/*************************************************************************tty.c
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

#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include <arch/cpu.h>
#include <asm/ioctls.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/io/term.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <lib/ctype.h>
#include <lib/mem.h>
#include <lib/str.h>
#include <lib/util.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <termios.h>
#include <vendor/tailq.h>

#define CONTROL_DEL 127

// #define TTY_HOST_OUTPUT

static size_t NextttyNum = 0;

// DEV OPS

static int tty_read(void     *buf,
                    size_t    buflen,
                    size_t   *bytes_read,
                    void     *devdata,
                    uintmax_t off,
                    uintmax_t flags) {
    (void)off;

    com_tty_t *tty_data = devdata;
    KASSERT(COM_TTY_TEXT == tty_data->type);
    com_text_tty_t *tty = &tty_data->tty.text;

    return kringbuffer_read(buf,
                            bytes_read,
                            &tty->backend.slave_rb,
                            buflen,
                            !(O_NONBLOCK & flags),
                            com_io_tty_text_backend_poll_callback,
                            &tty->backend,
                            NULL);
}

static int tty_write(size_t   *bytes_written,
                     void     *devdata,
                     void     *buf,
                     size_t    buflen,
                     uintmax_t off,
                     uintmax_t flags) {
    (void)off;
    (void)flags;

    com_tty_t *tty_data = devdata;
    KASSERT(COM_TTY_TEXT == tty_data->type);
    com_text_tty_t *tty = &tty_data->tty.text;

    com_io_term_putsn(tty->term, buf, buflen);
#if defined(DISABLE_LOGGING) && defined(TTY_HOST_OUTPUT)
    kprintf("%.*s", (int)buflen, buf);
#endif
    *bytes_written = buflen;
    return 0;
}

static int tty_ioctl(void *devdata, uintmax_t op, void *buf) {
    com_tty_t *tty_data = devdata;
    KASSERT(COM_TTY_TEXT == tty_data->type);
    com_text_tty_t *tty        = &tty_data->tty.text;
    tcflag_t        prev_lflag = tty->backend.termios.c_lflag;

    int ret =
        com_io_tty_text_backend_ioctl(&tty->backend, tty_data->vnode, op, buf);

    if (prev_lflag == tty->backend.termios.c_lflag) {
        return ret;
    }

    // Ensure that canonical mode is always buffered and raw mode isn't so to
    // reduce latency in raw mode and minimize useless overhead in canonical
    // mode
    if (ICANON & tty->backend.termios.c_lflag) {
        com_io_term_set_buffering(tty->term, true);
    } else {
        com_io_term_set_buffering(tty->term, false);
    }

    return ret;
}

// NOTE: returns 0 because it returns the value of errno
static int tty_isatty(void *devdata) {
    (void)devdata;
    return 0;
}

static int tty_close(void *devdata) {
    (void)devdata;
    KASSERT(false);
}

static int tty_stat(struct stat *out, void *devdata) {
    out->st_blksize = 0;
    out->st_ino     = (ino_t)devdata;
    out->st_mode    = 0622; // apparently makes xterm happy
    out->st_mode |= S_IFCHR;
    return 0;
}

static int tty_poll_head(com_poll_head_t **out, void *devdata) {
    com_tty_t *tty_data = devdata;
    KASSERT(COM_TTY_TEXT == tty_data->type);
    com_text_tty_t *tty = &tty_data->tty.text;
    *out                = &tty->backend.slave_ph;
    return 0;
}

static int tty_poll(short *revents, void *devdata, short events) {
    (void)events;
    com_tty_t *tty_data = devdata;
    KASSERT(COM_TTY_TEXT == tty_data->type);
    com_text_tty_t *tty = &tty_data->tty.text;
    short           out = POLLOUT;

    if (tty->backend.slave_rb.write.index != tty->backend.slave_rb.read.index) {
        out |= POLLIN;
    }

    *revents = out;
    return 0;
}

static com_dev_ops_t TextTtyDevOps = {.read      = tty_read,
                                      .write     = tty_write,
                                      .ioctl     = tty_ioctl,
                                      .isatty    = tty_isatty,
                                      .close     = tty_close,
                                      .stat      = tty_stat,
                                      .poll_head = tty_poll_head,
                                      .poll      = tty_poll};

static void text_tty_kbd_in(com_tty_t *self, char c, uintmax_t mod) {
    bool is_arrow     = COM_IO_TTY_MOD_ARROW & mod;
    bool is_ctrl_held = COM_IO_TTY_MOD_LCTRL & mod;
    bool is_del       = false;

    KASSERT(COM_TTY_TEXT == self->type);
    com_text_tty_t *tty = &self->tty.text;

    if (CONTROL_DEL == c) {
        is_del = true;
    }

    if ('\b' == c) {
        c = CONTROL_DEL;
    }

    if (is_arrow || is_del) {
        char   escape[4] = "\e[";
        size_t len       = 2;

        if (is_arrow) {
            escape[2] = c;
            len++;
        } else {
            escape[2] = '3';
            escape[3] = '~';
            len += 2;
        }

        com_io_tty_process_chars(NULL, &tty->backend, escape, len, false, tty);
        return;
    }

    if (is_ctrl_held) {
        if (KISLOWER(c)) {
            c -= 32;
        }
        c -= 64;
    }

    com_io_tty_process_char(&tty->backend, c, false, tty);
}

static int text_tty_echo(size_t     *bytes_written,
                         const char *buf,
                         size_t      buflen,
                         bool        blocking,
                         void       *passthrough) {
    (void)blocking;

    com_text_tty_t *tty = passthrough;
    com_io_term_putsn(tty->term, buf, buflen);
    if (NULL != bytes_written) {
        *bytes_written = buflen;
    }
    return 0;
}

int com_io_tty_text_backend_ioctl(com_text_tty_backend_t *tty_backend,
                                  com_vnode_t            *tty_vn,
                                  uintmax_t               op,
                                  void                   *buf) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    if (TIOCGWINSZ == op) {
        struct winsize *ws = buf;
        ws->ws_row         = tty_backend->rows;
        ws->ws_col         = tty_backend->cols;
        return 0;
    }

    if (TCGETS == op) {
        struct termios *termios = buf;
        *termios                = tty_backend->termios;
        return 0;
    }

    if (TCSETS == op || TCSETSF == op || TCSETSW == op) {
        struct termios *termios = buf;
        tty_backend->termios    = *termios;
        return 0;
    }

    if (TIOCGPGRP == op) {
        if (NULL == curr_proc->proc_group ||
            NULL == curr_proc->proc_group->session ||
            tty_vn != curr_proc->proc_group->session->tty) {
            return ENOTTY;
        }

        if (0 != tty_backend->fg_pgid) {
            *(pid_t *)buf = tty_backend->fg_pgid;
        } else {
            *(pid_t *)buf = 1000000; // POSIX says this must be an arbitrarly
                                     // high number which is not assigned to
                                     // anyu process or process group
        }

        return 0;
    }

    if (TIOCSCTTY == op) {
        if (NULL == curr_proc->proc_group ||
            NULL == curr_proc->proc_group->session) {
            return ENOTTY; // TODO: probably wrog errno
        }

        curr_proc->proc_group->session->tty = tty_vn;
        tty_backend->fg_pgid                = curr_proc->proc_group->pgid;
        return 0;
    }

    if (TIOCSPGRP == op) {
        if (NULL == curr_proc->proc_group ||
            NULL == curr_proc->proc_group->session ||
            tty_vn != curr_proc->proc_group->session->tty) {
            return ENOTTY;
        }

        tty_backend->fg_pgid = *(pid_t *)buf;
        return 0;
    }

    KDEBUG("unsupported ioctl %x", op);
    return ENOSYS;
}

void com_io_tty_text_backend_poll_callback(void *data) {
    com_text_tty_backend_t *backend = data;

    com_spinlock_acquire(&backend->slave_ph.lock);
    com_polled_t *polled, *_;
    LIST_FOREACH_SAFE(polled, &backend->slave_ph.polled_list, polled_list, _) {
        com_spinlock_acquire(&polled->poller->lock);
        com_sys_sched_notify_all(&polled->poller->waiters);
        com_spinlock_release(&polled->poller->lock);
    }
    com_spinlock_release(&backend->slave_ph.lock);
}

// CREDIT: vloxei64/ke
// (reworked somewhat now, will probably be reworked more soon)
int com_io_tty_process_char(com_text_tty_backend_t *tty_backend,
                            char                    c,
                            bool                    blocking,
                            void                   *passthrough) {
    bool handled   = false;
    bool line_done = false;
    int  sig       = COM_SYS_SIGNAL_NONE;
    int  ret       = 0;

    if (ISIG & tty_backend->termios.c_lflag) {
        if (tty_backend->termios.c_cc[VINTR] == c) {
            ret     = tty_backend->echo(NULL, "^C", 2, blocking, passthrough);
            sig     = SIGINT;
            handled = true;
        }
        if (tty_backend->termios.c_cc[VQUIT] == c) {
            ret = tty_backend->echo(NULL, "^\\\\", 3, blocking, passthrough);
            sig = SIGQUIT;
            handled = true;
        }
        if (tty_backend->termios.c_cc[VSUSP] == c) {
            sig     = SIGTSTP;
            handled = true;
        }
    }

    if (0 != ret) {
        goto end;
    }

    if ('\r' == c && IGNCR & tty_backend->termios.c_iflag) {
        return 0;
    }

    if ('\r' == c && ICRNL & tty_backend->termios.c_iflag) {
        c = '\n';
    }

    if ('\n' == c && INLCR & tty_backend->termios.c_iflag) {
        c = '\r';
    }

    // handle raw mode
    if (!(ICANON & tty_backend->termios.c_lflag)) {
        ret     = kringbuffer_write(NULL,
                                &tty_backend->slave_rb,
                                &c,
                                1,
                                false,
                                com_io_tty_text_backend_poll_callback,
                                tty_backend,
                                NULL);
        handled = true;
        goto end;
    }

    // handle canon mode

    if (tty_backend->termios.c_cc[VEOF] == c) {
        handled   = true;
        line_done = true;

        if (0 == tty_backend->canon.index) {
            tty_backend->slave_rb.is_eof = true;
        }
    }

    if ('\n' == c || '\r' == c) {
        line_done = true;
    }

    if (tty_backend->termios.c_cc[VERASE] == c) {
        if (tty_backend->canon.index > 0) {
            if (ECHOE & tty_backend->termios.c_lflag) {
                ret =
                    tty_backend->echo(NULL, "\b \b", 3, blocking, passthrough);
                if (0 != ret) {
                    goto end;
                }
            }
            tty_backend->canon.index--;
        }

        handled = true;
    }

    // kill line
    if (tty_backend->termios.c_cc[VKILL] == c) {
        while (tty_backend->canon.index > 0) {
            if ('\n' == tty_backend->canon.buffer[tty_backend->canon.index]) {
                break;
            }

            tty_backend->canon.index--;
            if (ECHOKE & tty_backend->termios.c_lflag) {
                ret =
                    tty_backend->echo(NULL, "\b \b", 3, blocking, passthrough);
                if (0 != ret) {
                    goto end;
                }
            }
        }

        handled = true;
    }

end:
    if (!handled) {
        tty_backend->canon.buffer[tty_backend->canon.index++] = c;
        if (ECHO & tty_backend->termios.c_lflag) {
            ret = tty_backend->echo(NULL, &c, 1, blocking, passthrough);
            if (0 != ret) {
                goto end;
            }
        }
    }

    if (line_done) {
        size_t nbytes            = tty_backend->canon.index;
        ret                      = kringbuffer_write(NULL,
                                &tty_backend->slave_rb,
                                tty_backend->canon.buffer,
                                nbytes,
                                blocking,
                                com_io_tty_text_backend_poll_callback,
                                tty_backend,
                                NULL);
        tty_backend->canon.index = 0;
    }

    if (COM_SYS_SIGNAL_NONE != sig) {
        com_sys_signal_send_to_proc_group(tty_backend->fg_pgid, sig, NULL);
    }

    return ret;
}

int com_io_tty_process_chars(size_t                 *bytes_processed,
                             com_text_tty_backend_t *tty_backend,
                             const char             *buf,
                             size_t                  buflen,
                             bool                    blocking,
                             void                   *passthrough) {
    int    ret = 0;
    size_t i   = 0;
    for (; i < buflen; i++) {
        ret =
            com_io_tty_process_char(tty_backend, buf[i], blocking, passthrough);
        if (0 != ret) {
            break;
        }
    }

    // If at least one char was written, there is no error
    if (0 != i) {
        ret = 0;
    }

    if (NULL != bytes_processed) {
        *bytes_processed = i;
    }

    return ret;
}

void com_io_tty_kbd_in(com_tty_t *tty, char c, uintmax_t mod) {
    tty->kbd_in(tty, c, mod);
}

void com_io_tty_init_text_backend(com_text_tty_backend_t *backend,
                                  size_t                  rows,
                                  size_t                  cols,
                                  int (*echo)(size_t     *bytes_written,
                                              const char *buf,
                                              size_t      buflen,
                                              bool        blocking,
                                              void       *passthrough)) {
    kmemset(backend, sizeof(com_text_tty_backend_t), 0);
    KRINGBUFFER_INIT(&backend->slave_rb);
    LIST_INIT(&backend->slave_ph.polled_list);
    backend->slave_ph.lock = COM_SPINLOCK_NEW();
    backend->echo          = echo;

    backend->rows = rows;
    backend->cols = cols;

    backend->termios.c_cc[VINTR]    = CINTR;
    backend->termios.c_cc[VQUIT]    = CQUIT;
    backend->termios.c_cc[VERASE]   = CERASE;
    backend->termios.c_cc[VKILL]    = CKILL;
    backend->termios.c_cc[VEOF]     = CEOF;
    backend->termios.c_cc[VTIME]    = CTIME;
    backend->termios.c_cc[VMIN]     = CMIN;
    backend->termios.c_cc[VSWTC]    = 0;
    backend->termios.c_cc[VSTART]   = CSTART;
    backend->termios.c_cc[VSTOP]    = CSTOP;
    backend->termios.c_cc[VSUSP]    = CSUSP;
    backend->termios.c_cc[VEOL]     = CEOL;
    backend->termios.c_cc[VREPRINT] = CREPRINT;
    backend->termios.c_cc[VDISCARD] = 0;
    backend->termios.c_cc[VWERASE]  = CWERASE;
    backend->termios.c_cc[VLNEXT]   = 0;
    backend->termios.c_cflag        = TTYDEF_CFLAG;
    backend->termios.c_iflag        = TTYDEF_IFLAG;
    backend->termios.c_lflag        = TTYDEF_LFLAG;
    backend->termios.c_oflag        = TTYDEF_OFLAG;
#ifdef __linux__
    backend->termios.c_ispeed = backend->termios.c_ospeed = TTYDEF_SPEED;
#else
    backend->termios.ibaud = backend->termios.obaud = TTYDEF_SPEED;
#endif
}

int com_io_tty_init_text(com_vnode_t **out,
                         com_tty_t   **out_tty,
                         com_term_t   *term) {
    size_t tty_num = __atomic_fetch_add(&NextttyNum, 1, __ATOMIC_SEQ_CST);
    KLOG("initializing text tty %u", tty_num);

    com_tty_t *tty_data = com_mm_slab_alloc(sizeof(com_tty_t));
    tty_data->type      = COM_TTY_TEXT;
    tty_data->kbd_in    = text_tty_kbd_in;
    tty_data->num       = tty_num;
    com_text_tty_t *tty = &tty_data->tty.text;
    tty->term           = term;

    // Poor man's sprintf
    char tty_num_str[24];
    kuitoa(tty_num, tty_num_str);
    char tty_name[3 + 24] = {0}; // "tty" + number
    kstrcpy(tty_name, "tty");
    kstrcpy(&tty_name[3], tty_num_str);

    com_vnode_t *tty_dev;
    int          ret = com_fs_devfs_register(
        &tty_dev, NULL, tty_name, kstrlen(tty_name), &TextTtyDevOps, tty_data);

    if (0 != ret) {
        tty_dev = NULL;
        goto end;
    }

    tty_data->vnode = tty_dev;

    size_t rows, cols;
    com_io_term_get_size(term, &rows, &cols);
    com_io_tty_init_text_backend(&tty->backend, rows, cols, text_tty_echo);

end:
    if (NULL != out) {
        *out = tty_dev;
    }

    if (NULL != out_tty) {
        *out_tty = tty_data;
    }

    KDEBUG("created tty with name %s", tty_name);
    return ret;
}

// DEVTTY FUNCTIONS

static int devtty_open(com_vnode_t **out, void *devdata) {
    (void)devdata;
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_vnode_t *ctty = curr_proc->proc_group->session->tty;

    if (NULL != ctty) {
        COM_FS_VFS_VNODE_HOLD(ctty);
        *out = ctty;
        return 0;
    }

    *out = NULL;
    return ENXIO;
}

static com_dev_ops_t TtyCloneDevOps = {.open = devtty_open, .stat = tty_stat};

int com_io_tty_devtty_init(com_vnode_t **devtty) {
    KLOG("initializing /dev/tty clone device");

    com_vnode_t *dev = NULL;
    int          ret =
        com_fs_devfs_register(&dev, NULL, "tty", 3, &TtyCloneDevOps, NULL);

    *devtty = dev;
    return ret;
}
