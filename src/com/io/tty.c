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
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <lib/ctype.h>
#include <lib/mem.h>
#include <lib/spinlock.h>
#include <lib/str.h>
#include <lib/util.h>
#include <poll.h>
#include <salernos/ktty.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <termios.h>
#include <vendor/tailq.h>

#define CONTROL_DEL 127

static size_t NextttyNum = 0;

// TEXT TTY FUNCTIONS

static int text_tty_echo(size_t     *bytes_written,
                         const char *buf,
                         size_t      buflen,
                         bool        blocking,
                         void       *passthrough) {
    (void)blocking;

    com_text_tty_t *tty = passthrough;
    com_io_term_putsn(tty->term, buf, buflen);
#if CONFIG_LOG_LEVEL == CONST_LOG_LEVEL_TTY
    printf("%.*s", (int)buflen, buf);
#endif
    *bytes_written = buflen;
    return 0;
}

static void text_tty_kbd_convert(com_text_tty_t *tty, char c, int mod) {
    bool is_arrow     = COM_IO_TTY_MOD_ARROW & mod;
    bool is_ctrl_held = COM_IO_TTY_MOD_LCTRL & mod;
    bool is_del       = false;

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

static void text_tty_kbd_in(com_tty_t *self, com_kbd_packet_t *kbd_pkt) {
    static int mod = 0;

    KASSERT(E_COM_TTY_TYPE_TEXT == self->type);
    com_text_tty_t *tty = &self->tty.text;

    if (COM_IO_KEYBOARD_KEYSTATE_RELEASED == kbd_pkt->keystate) {
        switch (kbd_pkt->keycode) {
            case COM_IO_KEYBOARD_KEY_LEFTCTRL:
                mod &= ~(COM_IO_TTY_MOD_LCTRL);
                return;
            case COM_IO_KEYBOARD_KEY_RIGHTCTRL:
                mod &= ~(COM_IO_TTY_MOD_RCTRL);
                return;
            case COM_IO_KEYBOARD_KEY_LEFTALT:
                mod &= ~(COM_IO_TTY_MOD_LALT);
                return;
            case COM_IO_KEYBOARD_KEY_RIGHTALT:
                mod &= ~(COM_IO_TTY_MOD_RALT);
                return;
            case COM_IO_KEYBOARD_KEY_LEFTSHIFT:
                mod &= ~(COM_IO_TTY_MOD_LSHIFT);
                return;
            case COM_IO_KEYBOARD_KEY_RIGHTSHIFT:
                mod &= ~(COM_IO_TTY_MOD_RSHIFT);
                return;
            default:
                return;
        }
    }

    // Here COM_IO_KEYBOARD_KEYSTATE_PRESSED == kbd_pkt->keystate
    switch (kbd_pkt->keycode) {
        // Modifiers
        case COM_IO_KEYBOARD_KEY_LEFTSHIFT:
            mod = mod | COM_IO_TTY_MOD_LSHIFT;
            return;
        case COM_IO_KEYBOARD_KEY_RIGHTSHIFT:
            mod = mod | COM_IO_TTY_MOD_RSHIFT;
            return;
        case COM_IO_KEYBOARD_KEY_LEFTCTRL:
            mod = mod | COM_IO_TTY_MOD_LCTRL;
            return;
        case COM_IO_KEYBOARD_KEY_RIGHTCTRL:
            mod = mod | COM_IO_TTY_MOD_RCTRL;
            return;
        case COM_IO_KEYBOARD_KEY_LEFTALT:
            mod = mod | COM_IO_TTY_MOD_LALT;
            return;
        case COM_IO_KEYBOARD_KEY_RIGHTALT:
            mod = mod | COM_IO_TTY_MOD_RALT;
            return;

        // Special keys
        case COM_IO_KEYBOARD_KEY_BACKSPACE:
            text_tty_kbd_convert(tty, 127, mod);
            break;

        // Arrow keys
        case COM_IO_KEYBOARD_KEY_UP:
            text_tty_kbd_convert(tty, 'A', mod | COM_IO_TTY_MOD_ARROW);
            return;
        case COM_IO_KEYBOARD_KEY_DOWN:
            text_tty_kbd_convert(tty, 'B', mod | COM_IO_TTY_MOD_ARROW);
            return;
        case COM_IO_KEYBOARD_KEY_RIGHT:
            text_tty_kbd_convert(tty, 'C', mod | COM_IO_TTY_MOD_ARROW);
            return;
        case COM_IO_KEYBOARD_KEY_LEFT:
            text_tty_kbd_convert(tty, 'D', mod | COM_IO_TTY_MOD_ARROW);
            return;

        default:
            break;
    }

    // This is called by the kernel console, which is called by the keyboard
    // drivcer AFTER calling the layout driver, so we can use the c field
    text_tty_kbd_convert(tty, kbd_pkt->c, mod);
}

// FRAMEBUFFER TTY FUNCTIONS

static void fb_tty_kbd_in(com_tty_t *self, com_kbd_packet_t *kbd_pkt) {
    (void)self;
    com_io_keyboard_send_packet(kbd_pkt->keyboard, kbd_pkt);
}

static void fb_tty_mouse_in(com_tty_t *self, com_mouse_packet_t *mouse_pkt) {
    (void)self;
    com_io_mouse_send_packet(mouse_pkt->mouse, mouse_pkt);
}

// DEV OPS

static int tty_read(void     *buf,
                    size_t    buflen,
                    size_t   *bytes_read,
                    void     *devdata,
                    uintmax_t off,
                    uintmax_t flags) {
    (void)off;

    com_tty_t *tty_data = devdata;
    if (E_COM_TTY_TYPE_TEXT != tty_data->type) {
        *bytes_read = 0;
        return 0;
    }

    com_text_tty_t *tty = &tty_data->tty.text;
    return kringbuffer_read(buf,
                            bytes_read,
                            &tty->backend.slave_rb,
                            buflen,
                            KRINGBUFFER_NOATOMIC,
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
    if (E_COM_TTY_TYPE_TEXT != tty_data->type) {
        *bytes_written = 0;
        return 0;
    }

    com_text_tty_t *tty = &tty_data->tty.text;
    return com_io_tty_text_backend_echo(bytes_written,
                                        &tty->backend,
                                        buf,
                                        buflen,
                                        true,
                                        tty);
}

static int tty_ioctl(void *devdata, uintmax_t op, void *buf) {
    com_tty_t *tty_data = devdata;

    // In SalernOS, TTYs can either be graphical or text mode. Text mode TTYs
    // have all the features implemented in this file and use a terminal
    // backend, whereas graphical TTYs have none of these features and use a
    // framebuffer backend. Userspace is responsible for management of graphical
    // TTYs.
    if (KTTY_IOCTL_SETMODE == op) {
        uintptr_t mode = (uintptr_t)buf;
        switch (mode) {
            case KTTY_MODE_TEXT:
                tty_data->type     = E_COM_TTY_TYPE_FRAMEBUFFER;
                tty_data->kbd_in   = text_tty_kbd_in;
                tty_data->mouse_in = NULL;
                com_io_term_enable(tty_data->tty.text.term);
                return 0;
            case KTTY_MODE_GRAPHICS:
                tty_data->type     = E_COM_TTY_TYPE_FRAMEBUFFER;
                tty_data->kbd_in   = fb_tty_kbd_in;
                tty_data->mouse_in = fb_tty_mouse_in;
                com_io_term_disable(tty_data->tty.text.term);
                tty_data->tty.fb.owner = tty_data->tty.text.backend.fg_pgid;
                return 0;
            default:
                return EINVAL;
        }
    }

    if (E_COM_TTY_TYPE_TEXT != tty_data->type) {
        return ENOSYS;
    }

    com_text_tty_t *tty = &tty_data->tty.text;
    return com_io_tty_text_backend_ioctl(&tty->backend,
                                         tty_data->vnode,
                                         op,
                                         buf);
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

    if (E_COM_TTY_TYPE_TEXT == tty_data->type) {
        com_text_tty_t *tty = &tty_data->tty.text;
        *out                = &tty->backend.slave_ph;
        return 0;
    }

    return ENOSYS;
}

static int tty_poll(short *revents, void *devdata, short events) {
    (void)events;
    com_tty_t *tty_data = devdata;
    if (E_COM_TTY_TYPE_TEXT != tty_data->type) {
        return ENOSYS;
    }

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

// TEXT TTY BACKEND INTERFACE

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
        ws->ws_ypixel      = 0;
        ws->ws_xpixel      = 0;
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

    KDEBUG("unsupported ioctl 0x%X", op);
    return ENOSYS;
}

int com_io_tty_text_backend_echo(size_t                 *bytes_written,
                                 com_text_tty_backend_t *tty_backend,
                                 const char             *buf,
                                 size_t                  buflen,
                                 bool                    blocking,
                                 void                   *passthrough) {
    size_t utmp;
    if (NULL == bytes_written) {
        bytes_written = &utmp;
    }

    if ((OPOST | ONLCR) & tty_backend->termios.c_oflag) {
        size_t count = 0;
        int    e     = 0;

        while (buflen > 0) {
            char *first_nl = kmemchr(buf, '\n', buflen);
            if (NULL == first_nl) {
                goto no_newline;
            }

            char  *echo_buf = (void *)buf;
            size_t bw       = 0;
            size_t echo_len = first_nl - buf;

            e = tty_backend->echo(&bw,
                                  echo_buf,
                                  echo_len,
                                  blocking,
                                  passthrough);
            count += bw + 1;
            if (0 != e) {
                break;
            }

            e = tty_backend->echo(&bw, "\r\n", 2, blocking, passthrough);
            if (0 != e) {
                break;
            }

            buf += echo_len + 1;
            buflen -= echo_len + 1;
        }

        *bytes_written = count;
        return e;
    }

no_newline:
    return tty_backend->echo(bytes_written, buf, buflen, blocking, passthrough);
}

void com_io_tty_text_backend_poll_callback(void *data) {
    com_text_tty_backend_t *backend = data;

    kspinlock_acquire(&backend->slave_ph.lock);
    com_polled_t *polled, *_;
    LIST_FOREACH_SAFE(polled, &backend->slave_ph.polled_list, polled_list, _) {
        kspinlock_acquire(&polled->poller->lock);
        com_sys_sched_notify_all(&polled->poller->waiters);
        kspinlock_release(&polled->poller->lock);
    }
    kspinlock_release(&backend->slave_ph.lock);
}

// CREDIT: vloxei64/ke
// (reworked somewhat now, will probably be reworked more soon)
int com_io_tty_process_char(com_text_tty_backend_t *tty_backend,
                            char                    c,
                            bool                    blocking,
                            void                   *passthrough) {
    bool handled   = false;
    bool line_done = false;
    int  sig       = COM_IPC_SIGNAL_NONE;
    int  ret       = 0;

    if (ISIG & tty_backend->termios.c_lflag) {
        if (tty_backend->termios.c_cc[VINTR] == c) {
            ret     = com_io_tty_text_backend_echo(NULL,
                                               tty_backend,
                                               "^C",
                                               2,
                                               blocking,
                                               passthrough);
            sig     = SIGINT;
            handled = true;
        }
        if (tty_backend->termios.c_cc[VQUIT] == c) {
            ret     = com_io_tty_text_backend_echo(NULL,
                                               tty_backend,
                                               "^\\\\",
                                               3,
                                               blocking,
                                               passthrough);
            sig     = SIGQUIT;
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
                                KRINGBUFFER_NOATOMIC,
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
                ret = com_io_tty_text_backend_echo(NULL,
                                                   tty_backend,
                                                   "\b \b",
                                                   3,
                                                   blocking,
                                                   passthrough);
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
                ret = com_io_tty_text_backend_echo(NULL,
                                                   tty_backend,
                                                   "\b \b",
                                                   3,
                                                   blocking,
                                                   passthrough);
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
            ret = com_io_tty_text_backend_echo(NULL,
                                               tty_backend,
                                               &c,
                                               1,
                                               blocking,
                                               passthrough);
            if (0 != ret) {
                goto sighandler;
            }
        }
    }

    if (line_done) {
        size_t nbytes            = tty_backend->canon.index;
        ret                      = kringbuffer_write(NULL,
                                &tty_backend->slave_rb,
                                tty_backend->canon.buffer,
                                nbytes,
                                KRINGBUFFER_NOATOMIC,
                                blocking,
                                com_io_tty_text_backend_poll_callback,
                                tty_backend,
                                NULL);
        tty_backend->canon.index = 0;
    }

sighandler:
    if (COM_IPC_SIGNAL_NONE != sig) {
        com_ipc_signal_send_to_proc_group(tty_backend->fg_pgid, sig, NULL);
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
        ret = com_io_tty_process_char(tty_backend,
                                      buf[i],
                                      blocking,
                                      passthrough);
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
    backend->slave_ph.lock = KSPINLOCK_NEW();
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

// KERNEL TTY INTERFACE

void com_io_tty_kbd_in(com_tty_t *tty, com_kbd_packet_t *kbd_pkt) {
    if (NULL != tty->kbd_in) {
        tty->kbd_in(tty, kbd_pkt);
    }
}

void com_io_tty_mouse_in(com_tty_t *tty, com_mouse_packet_t *mouse_pkt) {
    if (NULL != tty->mouse_in) {
        tty->mouse_in(tty, mouse_pkt);
    }
}

int com_io_tty_init_text(com_vnode_t **out,
                         com_tty_t   **out_tty,
                         com_term_t   *term) {
    size_t tty_num = __atomic_fetch_add(&NextttyNum, 1, __ATOMIC_SEQ_CST);
    KLOG("initializing text tty %zu", tty_num);

    com_tty_t *tty_data  = com_mm_slab_alloc(sizeof(com_tty_t));
    tty_data->type       = E_COM_TTY_TYPE_TEXT;
    tty_data->kbd_in     = text_tty_kbd_in;
    tty_data->mouse_in   = NULL;
    tty_data->num        = tty_num;
    com_text_tty_t *tty  = &tty_data->tty.text;
    tty->term            = term;
    tty_data->panic_term = term;

    // Poor man's sprintf
    char tty_num_str[24];
    kuitoa(tty_num, tty_num_str);
    char tty_name[3 + 24] = {0}; // "tty" + number
    kstrcpy(tty_name, "tty");
    kstrcpy(&tty_name[3], tty_num_str);

    com_vnode_t *tty_dev;
    int          ret = com_fs_devfs_register(&tty_dev,
                                    NULL,
                                    tty_name,
                                    kstrlen(tty_name),
                                    &TextTtyDevOps,
                                    tty_data);

    if (0 != ret) {
        tty_dev = NULL;
        goto end;
    }

    tty_dev->type   = E_COM_VNODE_TYPE_CHARDEV;
    tty_data->vnode = tty_dev;
    KDEBUG("tty vn=%X");

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
    int          ret = com_fs_devfs_register(&dev,
                                    NULL,
                                    "tty",
                                    3,
                                    &TtyCloneDevOps,
                                    NULL);

    *devtty = dev;
    return ret;
}
