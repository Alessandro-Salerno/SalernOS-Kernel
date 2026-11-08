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

#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include <arch/cpu.h>
#include <asm/ioctls.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/console.h>
#include <kernel/com/io/log.h>
#include <kernel/com/io/term.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <lib/ctype.h>
#include <lib/mem.h>
#include <lib/mutex.h>
#include <lib/spinlock.h>
#include <lib/str.h>
#include <lib/usercopy.h>
#include <lib/util.h>
#include <limits.h>
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
    bool is_ctrl_held = COM_IO_KEYBOARD_MOD_LCTRL & mod;
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
    KASSERT(E_COM_TTY_TYPE_TEXT == self->type);
    com_text_tty_t *tty = &self->tty.text;
    int             mod = kbd_pkt->keyboard->state.mod;

    if (COM_IO_KEYBOARD_KEYSTATE_RELEASED == kbd_pkt->keystate) {
        return;
    }

    switch (kbd_pkt->keycode) {
        // Modifiers
        case COM_IO_KEYBOARD_KEY_LEFTSHIFT:
        case COM_IO_KEYBOARD_KEY_RIGHTSHIFT:
        case COM_IO_KEYBOARD_KEY_LEFTCTRL:
        case COM_IO_KEYBOARD_KEY_RIGHTCTRL:
        case COM_IO_KEYBOARD_KEY_LEFTALT:
        case COM_IO_KEYBOARD_KEY_RIGHTALT:
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

// KTTY FUNCTIONS

static bool ktty_is_process_switched(com_tty_t *tty) {
    return E_COM_TTY_TYPE_FRAMEBUFFER == tty->type &&
           KTTY_SWITCH_PROCESS == tty->tty.fb.switchmode.mode;
}

static bool ktty_signal_owner(com_tty_t *tty, int sig) {
    if (0 == com_ipc_signal_send_to_proc(tty->tty.fb.owner, sig, NULL)) {
        return true;
    }

    // NOTE: the owner is gone, so we set it back to auto mode
    tty->tty.fb.switchmode.mode = KTTY_SWITCH_AUTO;
    return false;
}

static int ktty_set_mode(com_tty_t *tty, uintptr_t mode) {
    if (KTTY_MODE_TEXT != mode && KTTY_MODE_GRAPHICS != mode) {
        return EINVAL;
    }

    kmutex_acquire(&tty->lock);
    bool fg = com_io_console_is_foreground(tty);

    if (KTTY_MODE_TEXT == mode) {
        tty->type     = E_COM_TTY_TYPE_TEXT;
        tty->kbd_in   = text_tty_kbd_in;
        tty->mouse_in = NULL;
        tty->tty.fb   = (com_fb_tty_t){0};
        if (fg) {
            com_io_term_enable(tty->tty.text.term);
        }
    } else {
        tty->type     = E_COM_TTY_TYPE_FRAMEBUFFER;
        tty->kbd_in   = fb_tty_kbd_in;
        tty->mouse_in = fb_tty_mouse_in;
        tty->tty.fb = (com_fb_tty_t){.owner = ARCH_CPU_GET_THREAD()->proc->pid};
        if (fg) {
            com_io_term_disable(tty->tty.text.term);
        }
    }

    kmutex_release(&tty->lock);
    // Nobody is left to answer a pending release request
    com_io_console_release(tty, true);
    return 0;
}

static int ktty_set_switch_mode(com_tty_t *tty, struct ktty_switchmode *mode) {
    if (KTTY_SWITCH_AUTO != mode->mode && KTTY_SWITCH_PROCESS != mode->mode) {
        return EINVAL;
    }

    int ret = 0;
    kmutex_acquire(&tty->lock);

    if (E_COM_TTY_TYPE_FRAMEBUFFER != tty->type) {
        ret = EINVAL;
    } else if (ARCH_CPU_GET_THREAD()->proc->pid != tty->tty.fb.owner) {
        ret = EPERM;
    } else {
        tty->tty.fb.switchmode = *mode;
    }

    kmutex_release(&tty->lock);

    if (0 == ret && KTTY_SWITCH_AUTO == mode->mode) {
        com_io_console_release(tty, true);
    }

    return ret;
}

static int ktty_release(com_tty_t *tty, uintptr_t answer) {
    bool allow = KTTY_RELEASE_ALLOW == answer;
    return com_io_console_release(tty, allow) ? 0 : EINVAL;
}

static int tty_ioctl(void *devdata, uintmax_t op, void *buf) {
    com_tty_t *tty_data = devdata;

    switch (op) {
        case KTTY_IOCTL_SETMODE:
            return ktty_set_mode(tty_data, (uintptr_t)buf);
        case KTTY_IOCTL_SETSWITCHMODE: {
            struct ktty_switchmode mode;
            int ret = kmemcpy_from_user(&mode, buf, sizeof(mode));
            return 0 != ret ? ret : ktty_set_switch_mode(tty_data, &mode);
        }
        case KTTY_IOCTL_RELEASE:
            return ktty_release(tty_data, (uintptr_t)buf);
        case KTTY_IOCTL_SWITCH:
            return com_io_console_switch((uintptr_t)buf) ? 0 : ENXIO;
        case KTTY_IOCTL_WAITFG:
            return com_io_console_wait_foreground(tty_data) ? 0 : EINTR;
        case KTTY_IOCTL_GETNUM: {
            int num = tty_data->num;
            return kmemcpy_to_user(buf, &num, sizeof(num));
        }
        default:
            break;
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

static int tty_seekable(void *devdata) {
    (void)devdata;
    return ESPIPE;
}

static int tty_stat(struct stat *out, void *devdata) {
    (void)devdata;
    out->st_blksize = 0;
    out->st_mode    = 0622; // apparently makes xterm happy
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

static com_dev_ops_t TextTtyDevOps = {.type      = E_COM_VNODE_TYPE_CHARDEV,
                                      .read      = tty_read,
                                      .write     = tty_write,
                                      .ioctl     = tty_ioctl,
                                      .isatty    = tty_isatty,
                                      .seekable  = tty_seekable,
                                      .stat      = tty_stat,
                                      .poll_head = tty_poll_head,
                                      .poll      = tty_poll};

// TEXT TTY BACKEND INTERFACE

static bool is_ctty(com_proc_t *proc, com_vnode_t *tty_vn) {
    if (NULL == proc->proc_group || NULL == proc->proc_group->session) {
        return false;
    }

    // The tty may change concurrently, see TIOCSCTTY
    com_proc_session_t *session = proc->proc_group->session;
    return tty_vn == __atomic_load_n(&session->tty, __ATOMIC_RELAXED);
}

int com_io_tty_text_backend_ioctl(com_text_tty_backend_t *tty_backend,
                                  com_vnode_t            *tty_vn,
                                  uintmax_t               op,
                                  void                   *buf) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    if (TIOCGWINSZ == op) {
        struct winsize ws = {.ws_row = tty_backend->rows,
                             .ws_col = tty_backend->cols};
        return kmemcpy_to_user(buf, &ws, sizeof(ws));
    }

    if (FIONREAD == op) {
        ksync_acquire(&tty_backend->slave_rb.condvar);
        int avail = KRINGBUFFER_AVAIL_READ(&tty_backend->slave_rb);
        ksync_release(&tty_backend->slave_rb.condvar);
        return kmemcpy_to_user(buf, &avail, sizeof(avail));
    }

    if (TCGETS == op) {
        return kmemcpy_to_user(buf,
                               &tty_backend->termios,
                               sizeof(tty_backend->termios));
    }

    if (TCSETS == op || TCSETSF == op || TCSETSW == op) {
        struct termios termios;
        int            ret = kmemcpy_from_user(&termios, buf, sizeof(termios));
        if (0 == ret) {
            tty_backend->termios = termios;
        }
        return ret;
    }

    if (TIOCGPGRP == op) {
        if (!is_ctty(curr_proc, tty_vn)) {
            return ENOTTY;
        }

        // POSIX says this must be an arbitrarly high number which is not
        // assigned to anyu process or process group
        _Static_assert(sizeof(pid_t) == sizeof(int), "pid_t is not an int");
        pid_t pgid = 0 != tty_backend->fg_pgid ? tty_backend->fg_pgid : INT_MAX;
        return kmemcpy_to_user(buf, &pgid, sizeof(pgid));
    }

    if (TIOCSCTTY == op) {
        if (NULL == curr_proc->proc_group ||
            NULL == curr_proc->proc_group->session) {
            return ENOTTY; // TODO: probably wrog errno
        }

        // The session owns a reference to its controlling terminal
        com_proc_session_t *session = curr_proc->proc_group->session;
        COM_FS_VFS_VNODE_HOLD(tty_vn);
        kspinlock_acquire(&session->pg_lock);
        com_vnode_t *old = session->tty;
        __atomic_store_n(&session->tty, tty_vn, __ATOMIC_RELAXED);
        kspinlock_release(&session->pg_lock);
        COM_FS_VFS_VNODE_RELEASE(old);
        tty_backend->fg_pgid = curr_proc->proc_group->pgid;
        return 0;
    }

    if (TIOCSPGRP == op) {
        if (!is_ctty(curr_proc, tty_vn)) {
            return ENOTTY;
        }

        pid_t pgid;
        int   ret = kmemcpy_from_user(&pgid, buf, sizeof(pgid));
        if (0 == ret) {
            tty_backend->fg_pgid = pgid;
        }
        return ret;
    }

    KDEBUG("unsupported ioctl 0x%X", op);
    return ENOSYS;
}

static int backend_echo(size_t                 *bytes_written,
                        com_text_tty_backend_t *tty_backend,
                        const char             *buf,
                        size_t                  buflen,
                        bool                    blocking,
                        void                   *passthrough) {
    size_t utmp;
    if (NULL == bytes_written) {
        bytes_written = &utmp;
    }

    tcflag_t oflag = tty_backend->termios.c_oflag;
    if (!(OPOST & oflag) || !(ONLCR & oflag)) {
        return tty_backend->echo(bytes_written,
                                 buf,
                                 buflen,
                                 blocking,
                                 passthrough);
    }

    // A newline only counts as written once all of its \r\n is
    size_t count = 0;
    int    e     = 0;
    while (count < buflen) {
        const char *line = buf + count;
        const char *nl   = kmemchr(line, '\n', buflen - count);
        size_t      len  = NULL != nl ? (size_t)(nl - line) : buflen - count;
        size_t      bw   = 0;

        if (0 != len) {
            e = tty_backend->echo(&bw, line, len, blocking, passthrough);
            count += bw;
            if (0 != e || bw < len) {
                break;
            }
        }
        if (NULL == nl) {
            break;
        }

        bw = 0;
        e  = tty_backend->echo(&bw, "\r\n", 2, blocking, passthrough);
        if (0 != e || bw < 2) {
            break;
        }
        count++;
    }

    *bytes_written = count;
    return e;
}

// The backends may hold spinlocks, so they only ever see kernel memory
int com_io_tty_text_backend_echo(size_t                 *bytes_written,
                                 com_text_tty_backend_t *tty_backend,
                                 const char             *buf,
                                 size_t                  buflen,
                                 bool                    blocking,
                                 void                   *passthrough) {
    char   chunk[128];
    size_t done = 0;
    int    ret  = 0;

    while (done < buflen) {
        size_t n = KMIN(buflen - done, sizeof(chunk));
        size_t w = 0;
        ret      = kmemcpy_maybe_user(chunk, buf + done, n);
        if (0 != ret) {
            break;
        }
        ret = backend_echo(&w, tty_backend, chunk, n, blocking, passthrough);
        done += w;
        if (0 != ret || w < n) {
            break;
        }
    }

    if (NULL != bytes_written) {
        *bytes_written = done;
    }

    return 0 != done ? 0 : ret;
}

void com_io_tty_text_backend_poll_callback(void *data) {
    com_text_tty_backend_t *backend = data;

    kspinlock_acquire(&backend->slave_ph.lock);
    com_polled_t *polled, *_;
    LIST_FOREACH_SAFE(polled, &backend->slave_ph.polled_list, polled_list, _) {
        com_fs_poll_notify(polled->poller);
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
            ret     = backend_echo(NULL,
                                   tty_backend,
                                   "^C",
                                   2,
                                   blocking,
                                   passthrough);
            sig     = SIGINT;
            handled = true;
        }
        if (tty_backend->termios.c_cc[VQUIT] == c) {
            ret     = backend_echo(NULL,
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
                ret = backend_echo(NULL,
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
                ret = backend_echo(NULL,
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
            ret = backend_echo(NULL, tty_backend, &c, 1, blocking, passthrough);
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
    char   chunk[128];
    while (i < buflen) {
        size_t n = KMIN(buflen - i, sizeof(chunk));
        ret      = kmemcpy_maybe_user(chunk, buf + i, n);
        if (0 != ret) {
            break;
        }

        size_t j = 0;
        for (; j < n; j++) {
            ret = com_io_tty_process_char(tty_backend,
                                          chunk[j],
                                          blocking,
                                          passthrough);
            if (0 != ret) {
                break;
            }
        }

        i += j;
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
    KSYNC_INIT_MUTEX(&backend->slave_rb.condvar);
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
    kmutex_acquire(&tty->lock);
    if (NULL != tty->kbd_in) {
        tty->kbd_in(tty, kbd_pkt);
    }
    kmutex_release(&tty->lock);
}

void com_io_tty_mouse_in(com_tty_t *tty, com_mouse_packet_t *mouse_pkt) {
    kmutex_acquire(&tty->lock);
    if (NULL != tty->mouse_in) {
        tty->mouse_in(tty, mouse_pkt);
    }
    kmutex_release(&tty->lock);
}

void com_io_tty_enter_foreground(com_tty_t *tty) {
    kmutex_acquire(&tty->lock);
    if (E_COM_TTY_TYPE_TEXT == tty->type) {
        com_io_term_enable(tty->tty.text.term);
    } else if (ktty_is_process_switched(tty)) {
        ktty_signal_owner(tty, tty->tty.fb.switchmode.acqsig);
    }
    kmutex_release(&tty->lock);
}

void com_io_tty_leave_foreground(com_tty_t *tty) {
    kmutex_acquire(&tty->lock);
    if (E_COM_TTY_TYPE_TEXT == tty->type) {
        com_io_term_disable(tty->tty.text.term);
    }
    kmutex_release(&tty->lock);
}

bool com_io_tty_request_release(com_tty_t *tty) {
    kmutex_acquire(&tty->lock);
    bool wait = ktty_is_process_switched(tty) &&
                ktty_signal_owner(tty, tty->tty.fb.switchmode.relsig);
    kmutex_release(&tty->lock);
    return wait;
}

int com_io_tty_init_text(com_vnode_t **out,
                         com_tty_t   **out_tty,
                         com_term_t   *term) {
    size_t tty_num = __atomic_fetch_add(&NextttyNum, 1, __ATOMIC_SEQ_CST);
    KLOG("initializing text tty %zu", tty_num);

    com_tty_t *tty_data = com_mm_slab_alloc(sizeof(com_tty_t));
    KMUTEX_INIT(&tty_data->lock);
    tty_data->type       = E_COM_TTY_TYPE_TEXT;
    tty_data->kbd_in     = text_tty_kbd_in;
    tty_data->mouse_in   = NULL;
    tty_data->tty.fb     = (com_fb_tty_t){0};
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

    com_proc_session_t *session = curr_proc->proc_group->session;
    kspinlock_acquire(&session->pg_lock);
    com_vnode_t *ctty = session->tty;
    if (NULL != ctty) {
        COM_FS_VFS_VNODE_HOLD(ctty);
    }
    kspinlock_release(&session->pg_lock);

    // An open of the controlling terminal, so it gets its own close
    *out = NULL;
    if (NULL == ctty) {
        return ENXIO;
    }

    int ret = com_fs_vfs_open(out, ctty);
    COM_FS_VFS_VNODE_RELEASE(ctty);
    return ret;
}

static com_dev_ops_t TtyCloneDevOps = {.type = E_COM_VNODE_TYPE_CHARDEV,
                                       .open = devtty_open,
                                       .stat = tty_stat};

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
