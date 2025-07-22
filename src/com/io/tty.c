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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <termios.h>
#include <vendor/tailq.h>

#define CONTROL_DEL 127

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
                            NULL,
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
    *bytes_written = buflen;
    return 0;
}

static int tty_ioctl(void *devdata, uintmax_t op, void *buf) {
    com_tty_t *tty_data = devdata;
    KASSERT(COM_TTY_TEXT == tty_data->type);
    com_text_tty_t *tty = &tty_data->tty.text;

    if (TIOCGWINSZ == op) {
        struct winsize *ws = buf;
        ws->ws_row         = tty->backend.rows;
        ws->ws_col         = tty->backend.cols;
        return 0;
    }

    if (TCGETS == op) {
        struct termios *termios = buf;
        *termios                = tty->backend.termios;
        return 0;
    }

    if (TCSETS == op || TCSETSF == op || TCSETSW == op) {
        struct termios *termios = buf;
        tty->backend.termios    = *termios;
        return 0;
    }

    if (TIOCSPGRP == op) {
        tty->backend.fg_pgid = *(pid_t *)buf;
        KDEBUG("changed tty fg pgid to %d", tty->backend.fg_pgid);
        return 0;
    }

    return ENOSYS;
}

// NOTE: returns 0 because it returns the value of errno
static int tty_isatty(void *devdata) {
    (void)devdata;
    return 0;
}

static com_dev_ops_t TextTtyDevOps = {.read   = tty_read,
                                      .write  = tty_write,
                                      .ioctl  = tty_ioctl,
                                      .isatty = tty_isatty};

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

        com_io_tty_process_chars(&tty->backend, escape, len, false, tty);
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

static size_t text_tty_echo(const char *buf,
                            size_t      buflen,
                            bool        blocking,
                            void       *passthrough) {
    (void)blocking;

    com_text_tty_t *tty = passthrough;
    com_io_term_putsn(tty->term, buf, buflen);
    return buflen;
}

// CREDIT: vloxei64/ke
// (reworked somewhat now, will probably be reworked more soon)
void com_io_tty_process_char(com_text_tty_backend_t *tty_backend,
                             char                    c,
                             bool                    blocking,
                             void                   *passthrough) {
    bool handled   = false;
    bool line_done = false;
    int  sig       = COM_SYS_SIGNAL_NONE;

    if (ISIG & tty_backend->termios.c_lflag) {
        // TODO: print ^C
        if (tty_backend->termios.c_cc[VINTR] == c) {
            sig     = SIGINT;
            handled = true;
        }
        if (tty_backend->termios.c_cc[VQUIT] == c) {
            sig     = SIGQUIT;
            handled = true;
        }
        if (tty_backend->termios.c_cc[VSUSP] == c) {
            sig     = SIGTSTP;
            handled = true;
        }
    }

    if ('\r' == c && IGNCR & tty_backend->termios.c_iflag) {
        return;
    }

    if ('\r' == c && ICRNL & tty_backend->termios.c_iflag) {
        c = '\n';
    }

    if ('\n' == c && INLCR & tty_backend->termios.c_iflag) {
        c = '\r';
    }

    // handle raw mode
    if (!(ICANON & tty_backend->termios.c_lflag)) {
        kringbuffer_write(
            NULL, &tty_backend->slave_rb, &c, 1, false, NULL, NULL);
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
                tty_backend->echo("\b \b", 3, blocking, passthrough);
            }
            tty_backend->canon.index--;
        }

        handled = true;
    }

    // kill line
    if (tty_backend->termios.c_cc[VKILL] == c) {
        while (tty_backend->canon.index > 0) {
            if (tty_backend->canon.buffer[tty_backend->canon.index] == '\n' ||
                tty_backend->canon.buffer[tty_backend->canon.index] ==
                    tty_backend->termios.c_cc[VEOF] ||
                tty_backend->canon.buffer[tty_backend->canon.index] ==
                    tty_backend->termios.c_cc[VEOL]) {
                break;
            }

            tty_backend->canon.index--;
            if (ECHOKE & tty_backend->termios.c_lflag) {
                tty_backend->echo("\b \b", 3, blocking, passthrough);
            }
        }

        handled = true;
    }

end:
    if (!handled) {
        tty_backend->canon.buffer[tty_backend->canon.index++] = c;
        if (ECHO & tty_backend->termios.c_lflag) {
            tty_backend->echo(&c, 1, blocking, passthrough);
        }
    }

    if (line_done) {
        size_t nbytes = tty_backend->canon.index;
        kringbuffer_write(NULL,
                          &tty_backend->slave_rb,
                          tty_backend->canon.buffer,
                          nbytes,
                          blocking,
                          NULL,
                          NULL);
        tty_backend->canon.index = 0;
    }

    if (COM_SYS_SIGNAL_NONE != sig) {
        com_sys_signal_send_to_proc_group(tty_backend->fg_pgid, sig, NULL);
    }
}

void com_io_tty_process_chars(com_text_tty_backend_t *tty_backend,
                              const char             *buf,
                              size_t                  buflen,
                              bool                    blocking,
                              void                   *passthrough) {
    for (size_t i = 0; i < buflen; i++) {
        com_io_tty_process_char(tty_backend, buf[i], blocking, passthrough);
    }
}

void com_io_tty_kbd_in(com_tty_t *tty, char c, uintmax_t mod) {
    tty->kbd_in(tty, c, mod);
}

void com_io_tty_init_text_backend(com_text_tty_backend_t *backend,
                                  size_t                  rows,
                                  size_t                  cols,
                                  size_t (*echo)(const char *buf,
                                                 size_t      buflen,
                                                 bool        blocking,
                                                 void       *passthrough)) {
    kmemset(backend, sizeof(com_text_tty_backend_t), 0);
    KRINGBUFFER_INIT(&backend->slave_rb);
    backend->echo = echo;

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
    backend->termios.c_ispeed = backend->termios.c_ospeed = TTYDEF_SPEED;
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
