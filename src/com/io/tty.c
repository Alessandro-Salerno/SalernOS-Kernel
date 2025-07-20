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

// TODO: rewrite all of it using PTYs

#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include <arch/cpu.h>
#include <errno.h>
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
    (void)flags;

    com_tty_t *tty_data = devdata;
    KASSERT(COM_TTY_TEXT == tty_data->type);
    com_text_tty_t *tty = &tty_data->tty.text;

    size_t read_count = 0;
    com_spinlock_acquire(&tty->lock);

    while (true) {
        bool can_read  = false;
        char eol       = tty->termios.c_cc[VEOL];
        char eof       = tty->termios.c_cc[VEOF];
        bool eof_found = false;

        if (0 == eol) {
            eol = '\n';
        }

        size_t i = 0;
        for (; i < tty->write; i++) {
            const char c = tty->buf[i];

            // When using canonical mode, input is line-buffered. Thus, we only
            // allow reading when EOL or EOF are reached
            if ((ICANON & tty->termios.c_lflag) && (c == eol || c == eof)) {
                if (c == eol) {
                    i++;
                }

                eof_found = c == eof;
                can_read  = true;
                break;
            }

            // When operating in raw mode, input is not line-buffered. Thus we
            // stop reading when we reach the amount of characters requested by
            // the callee
            else if (!(ICANON & tty->termios.c_lflag) && i + 1 == buflen) {
                i++;
                can_read = true;
                break;
            }
        }

        if (!can_read) {
            com_sys_sched_wait(&tty->waitlist, &tty->lock);
            continue;
        }

        kmemcpy((uint8_t *)buf + read_count, tty->buf, KMIN(buflen, i));
        kmemmove(tty->buf,
                 (uint8_t *)tty->buf + KMIN(buflen, i),
                 tty->write - KMIN(buflen, i));
        tty->write -= KMIN(buflen, i);

        if (eof_found) {
            tty->write--;
        }

        read_count += KMIN(buflen, i);
        break;
    }

    com_spinlock_release(&tty->lock);
    *bytes_read = read_count;
    return 0;
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

    // com_spinlock_acquire(&Tty.write_lock);
    com_io_term_putsn(tty->term, buf, buflen);
    // com_spinlock_release(&Tty.write_lock);
    *bytes_written = buflen;
    return 0;
}

static int tty_ioctl(void *devdata, uintmax_t op, void *buf) {
    com_tty_t *tty_data = devdata;
    KASSERT(COM_TTY_TEXT == tty_data->type);
    com_text_tty_t *tty = &tty_data->tty.text;

    if (TIOCGWINSZ == op) {
        struct winsize *ws = buf;
        ws->ws_row         = tty->rows;
        ws->ws_col         = tty->cols;
        return 0;
    }

    if (TCGETS == op) {
        struct termios *termios = buf;
        *termios                = tty->termios;
        return 0;
    }

    if (TCSETS == op || TCSETSF == op || TCSETSW == op) {
        struct termios *termios = buf;
        tty->termios            = *termios;
        return 0;
    }

    return ENOSYS;
}

// NOTE: returns 0 because it returns the value of errno
int tty_isatty(void *devdata) {
    (void)devdata;
    return 0;
}

static com_dev_ops_t TextTtyDevOps = {.read   = tty_read,
                                      .write  = tty_write,
                                      .ioctl  = tty_ioctl,
                                      .isatty = tty_isatty};

static void text_tty_kbd_in(com_tty_t *self, char c, uintmax_t mod) {
    bool is_arrow     = COM_IO_TTY_MOD_ARROW & mod;
    bool is_del       = false;
    bool is_ctrl_held = COM_IO_TTY_MOD_LCTRL & mod;
    bool handled      = false;
    bool notify       = false;
    KASSERT(COM_TTY_TEXT == self->type);
    com_text_tty_t *tty = &self->tty.text;
    char            eol = tty->termios.c_cc[VEOL];

    if (0 == eol) {
        eol = '\n';
    }

    if (CONTROL_DEL == c) {
        is_del = true;
        goto end;
    }

    if ('\b' == c) {
        c = CONTROL_DEL;
    }

    if (is_ctrl_held) {
        if (KISLOWER(c)) {
            c -= 32;
        }
        c -= 64;
    }

    com_spinlock_acquire(&tty->lock);

    if (!is_arrow) {
        if (('\r' == c) && !(IGNCR & tty->termios.c_iflag) &&
            (ICRNL & tty->termios.c_iflag)) {
            c = '\n';
        } else if (('\n' == c) && (INLCR & tty->termios.c_iflag)) {
            c = '\r';
        }

        if (!(ICANON & tty->termios.c_lflag)) {
            notify = true;
            goto end;
        }

        if (eol == c) {
            notify = true;
        }

        if (tty->termios.c_cc[VEOF] == c) {
            tty->buf[tty->write] = c;
            tty->write++;
            handled = true;
        }

        if (tty->termios.c_cc[VERASE] == c) {
            if (tty->write > 0) {
                if (ECHOE & tty->termios.c_lflag) {
                    com_io_term_puts(tty->term, "\b \b");
                }

                if (tty->write > 0) {
                    tty->write--;
                }
            }

            handled = true;
        }
    }

end:
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

        kmemcpy((uint8_t *)tty->buf + tty->write, escape, len);
        tty->write += len;

        if (ECHO & tty->termios.c_lflag) {
            com_io_term_putsn(tty->term, escape, len);
            handled = true;
        }

        // temporary fix until I implement proper pty/tty
        if (!(ICANON & tty->termios.c_lflag)) {
            notify = true;
        }
    } else if (!handled) {
        tty->buf[tty->write] = c;
        tty->write++;
    }

    if ((ECHO & tty->termios.c_lflag) && !handled) {
        com_io_term_putc(tty->term, c);
    }

    if (notify && !TAILQ_EMPTY(&tty->waitlist)) {
        com_sys_sched_notify(&tty->waitlist);
        com_spinlock_release(&tty->lock);
        com_sys_sched_yield();
        return;
    }

    com_spinlock_release(&tty->lock);
}

void com_io_tty_kbd_in(com_tty_t *tty, char c, uintmax_t mod) {
    tty->kbd_in(tty, c, mod);
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
    tty->lock           = COM_SPINLOCK_NEW();

    TAILQ_INIT(&tty->waitlist);

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

    com_io_term_get_size(term, &tty->rows, &tty->cols);
    tty->termios.c_cc[VINTR]    = CINTR;
    tty->termios.c_cc[VQUIT]    = CQUIT;
    tty->termios.c_cc[VERASE]   = CERASE;
    tty->termios.c_cc[VKILL]    = CKILL;
    tty->termios.c_cc[VEOF]     = CEOF;
    tty->termios.c_cc[VTIME]    = CTIME;
    tty->termios.c_cc[VMIN]     = CMIN;
    tty->termios.c_cc[VSWTC]    = 0;
    tty->termios.c_cc[VSTART]   = CSTART;
    tty->termios.c_cc[VSTOP]    = CSTOP;
    tty->termios.c_cc[VSUSP]    = CSUSP;
    tty->termios.c_cc[VEOL]     = CEOL;
    tty->termios.c_cc[VREPRINT] = CREPRINT;
    tty->termios.c_cc[VDISCARD] = 0;
    tty->termios.c_cc[VWERASE]  = CWERASE;
    tty->termios.c_cc[VLNEXT]   = 0;
    tty->termios.c_cflag        = TTYDEF_CFLAG;
    tty->termios.c_iflag        = TTYDEF_IFLAG;
    tty->termios.c_lflag        = TTYDEF_LFLAG;
    tty->termios.c_oflag        = TTYDEF_OFLAG;
    tty->termios.c_ispeed = tty->termios.c_ospeed = TTYDEF_SPEED;

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
