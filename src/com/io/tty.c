#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include <arch/cpu.h>
#include <asm/ioctls.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/ctype.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/console.h>
#include <kernel/com/io/log.h>
#include <kernel/com/io/term.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sync/mutex.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <limits.h>
#include <poll.h>
#include <salernos/ktty.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <termios.h>
#include <vendor/tailq.h>

#define CONTROL_DEL 127

static usize g_next_tty_num = 0;

// TEXT Tty FUNCTIONS

static int text_tty_echo(const char *buf, usize buflen, bool blocking,
                         void *passthrough, usize *out_bytes_written) {
    (void)blocking;

    TextTTY *tty = passthrough;
    term_putsn(tty->term, buf, buflen);
#if CONFIG_LOG_LEVEL == CONST_LOG_LEVEL_TTY
    printf("%.*s", (int)buflen, buf);
#endif
    *out_bytes_written = buflen;
    return 0;
}

static void text_tty_kbd_convert(TextTTY *tty, char c, int mod) {
    bool is_arrow = mod & TTY_MOD_ARROW;
    bool is_ctrl_held = mod & KEYBOARD_MOD_LCTRL;
    bool is_del = false;

    if (c == CONTROL_DEL) is_del = true;
    if (c == '\b') c = CONTROL_DEL;

    if (is_arrow || is_del) {
        char escape[4] = "\e[";
        usize len = 2;

        if (is_arrow) {
            escape[2] = c;
            len++;
        } else {
            escape[2] = '3';
            escape[3] = '~';
            len += 2;
        }

        tty_process_chars(&tty->backend, escape, len, false, tty, NULL);
        return;
    }

    if (is_ctrl_held) {
        if (islower(c)) c -= 32;
        c -= 64;
    }

    tty_process_char(&tty->backend, c, false, tty);
}

static void text_tty_kbd_in(Tty *self, KbdPacket *kbd_pkt) {
    assert(self->type == TTY_TYPE_TEXT);
    TextTTY *tty = &self->tty.text;
    int mod = kbd_pkt->keyboard->state.mod;

    if (kbd_pkt->keystate == KEYBOARD_KEYSTATE_RELEASED) return;

    switch (kbd_pkt->keycode) {
        // Modifiers
        case KEYBOARD_KEY_LEFTSHIFT:
        case KEYBOARD_KEY_RIGHTSHIFT:
        case KEYBOARD_KEY_LEFTCTRL:
        case KEYBOARD_KEY_RIGHTCTRL:
        case KEYBOARD_KEY_LEFTALT:
        case KEYBOARD_KEY_RIGHTALT:
            return;

        // Special keys
        case KEYBOARD_KEY_BACKSPACE:
            text_tty_kbd_convert(tty, 127, mod);
            break;

        // Arrow keys
        case KEYBOARD_KEY_UP:
            text_tty_kbd_convert(tty, 'A', mod | TTY_MOD_ARROW);
            return;
        case KEYBOARD_KEY_DOWN:
            text_tty_kbd_convert(tty, 'B', mod | TTY_MOD_ARROW);
            return;
        case KEYBOARD_KEY_RIGHT:
            text_tty_kbd_convert(tty, 'C', mod | TTY_MOD_ARROW);
            return;
        case KEYBOARD_KEY_LEFT:
            text_tty_kbd_convert(tty, 'D', mod | TTY_MOD_ARROW);
            return;

        default:
            break;
    }

    // This is called by the kernel console, which is called by the keyboard
    // drivcer AFTER calling the layout driver, so we can use the c field
    text_tty_kbd_convert(tty, kbd_pkt->c, mod);
}

// FRAMEBUFFER Tty FUNCTIONS

static void fb_tty_kbd_in(Tty *self, KbdPacket *kbd_pkt) {
    (void)self;
    keyboard_send_packet(kbd_pkt->keyboard, kbd_pkt);
}

static void fb_tty_mouse_in(Tty *self, MousePacket *mouse_pkt) {
    (void)self;
    mouse_send_packet(mouse_pkt->mouse, mouse_pkt);
}

// DEV OPS

static int tty_read(void *buf, usize buflen, void *devdata, u64 off, u64 flags,
                    usize *out_bytes_read) {
    (void)off;

    Tty *tty_data = devdata;
    if (tty_data->type != TTY_TYPE_TEXT) {
        *out_bytes_read = 0;
        return 0;
    }

    TextTTY *tty = &tty_data->tty.text;
    return ringbuffer_read(buf, &tty->backend.slave_rb, buflen,
                           RINGBUFFER_NOATOMIC, !(flags & O_NONBLOCK),
                           tty_text_backend_poll_callback, &tty->backend, NULL,
                           out_bytes_read);
}

static int tty_write(void *devdata, void *buf, usize buflen, u64 off, u64 flags,
                     usize *out_bytes_written) {
    (void)off;
    (void)flags;

    Tty *tty_data = devdata;
    if (tty_data->type != TTY_TYPE_TEXT) {
        *out_bytes_written = 0;
        return 0;
    }

    TextTTY *tty = &tty_data->tty.text;
    return tty_text_backend_echo(&tty->backend, buf, buflen, true, tty,
                                 out_bytes_written);
}

// KTTY FUNCTIONS

static bool ktty_is_process_switched(Tty *tty) {
    return tty->type == TTY_TYPE_FRAMEBUFFER &&
        tty->tty.fb.switchmode.mode == KTTY_SWITCH_PROCESS;
}

static bool ktty_signal_owner(Tty *tty, int sig) {
    if (signal_send_to_proc(tty->tty.fb.owner, sig, NULL) == 0) return true;

    // NOTE: the owner is gone, so we set it back to auto mode
    tty->tty.fb.switchmode.mode = KTTY_SWITCH_AUTO;
    return false;
}

static int ktty_set_mode(Tty *tty, uptr mode) {
    if (mode != KTTY_MODE_TEXT && mode != KTTY_MODE_GRAPHICS) return EINVAL;

    mutex_acq(&tty->lock);
    bool fg = console_is_foreground(tty);

    if (mode == KTTY_MODE_TEXT) {
        tty->type = TTY_TYPE_TEXT;
        tty->kbd_in = text_tty_kbd_in;
        tty->mouse_in = NULL;
        tty->tty.fb = (FBTty){0};
        if (fg) term_enable(tty->tty.text.term);
    } else {
        tty->type = TTY_TYPE_FRAMEBUFFER;
        tty->kbd_in = fb_tty_kbd_in;
        tty->mouse_in = fb_tty_mouse_in;
        tty->tty.fb = (FBTty){.owner = cpu_get_thread()->proc->pid};
        if (fg) term_disable(tty->tty.text.term);
    }

    mutex_rel(&tty->lock);
    // Nobody is left to answer a pending release request
    console_rel(tty, true);
    return 0;
}

static int ktty_set_switch_mode(Tty *tty, struct ktty_switchmode *mode) {
    if (mode->mode != KTTY_SWITCH_AUTO && mode->mode != KTTY_SWITCH_PROCESS) {
        return EINVAL;
    }

    int ret = 0;
    mutex_acq(&tty->lock);

    if (tty->type != TTY_TYPE_FRAMEBUFFER) ret = EINVAL;
    else if (cpu_get_thread()->proc->pid != tty->tty.fb.owner) ret = EPERM;
    else tty->tty.fb.switchmode = *mode;

    mutex_rel(&tty->lock);

    if (ret == 0 && mode->mode == KTTY_SWITCH_AUTO) console_rel(tty, true);

    return ret;
}

static int ktty_rel(Tty *tty, uptr answer) {
    bool allow = answer == KTTY_RELEASE_ALLOW;
    return console_rel(tty, allow) ? 0 : EINVAL;
}

static int tty_ioctl(void *devdata, u64 op, void *buf) {
    Tty *tty_data = devdata;

    switch (op) {
        case KTTY_IOCTL_SETMODE:
            return ktty_set_mode(tty_data, (uptr)buf);
        case KTTY_IOCTL_SETSWITCHMODE: {
            struct ktty_switchmode mode;
            int ret = memcpy_from_user(&mode, buf, sizeof(mode));
            return ret != 0 ? ret : ktty_set_switch_mode(tty_data, &mode);
        }
        case KTTY_IOCTL_RELEASE:
            return ktty_rel(tty_data, (uptr)buf);
        case KTTY_IOCTL_SWITCH:
            return console_switch((uptr)buf) ? 0 : ENXIO;
        case KTTY_IOCTL_WAITFG:
            return console_wait_foreground(tty_data) ? 0 : EINTR;
        case KTTY_IOCTL_GETNUM: {
            int num = tty_data->num;
            return memcpy_to_user(buf, &num, sizeof(num));
        }
        default:
            break;
    }

    if (tty_data->type != TTY_TYPE_TEXT) return ENOSYS;

    TextTTY *tty = &tty_data->tty.text;
    return tty_text_backend_ioctl(&tty->backend, tty_data->vnode, op, buf);
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

static int tty_stat(void *devdata, struct stat *out_stat) {
    (void)devdata;
    out_stat->st_blksize = 0;
    out_stat->st_mode = 0622; // apparently makes xterm happy
    return 0;
}

static int tty_poll_head(void *devdata, PollHead **out_poll_head) {
    Tty *tty_data = devdata;

    if (tty_data->type == TTY_TYPE_TEXT) {
        TextTTY *tty = &tty_data->tty.text;
        *out_poll_head = &tty->backend.slave_ph;
        return 0;
    }

    return ENOSYS;
}

static int tty_poll(void *devdata, short events, short *out_revents) {
    (void)events;
    Tty *tty_data = devdata;
    if (tty_data->type != TTY_TYPE_TEXT) return ENOSYS;

    TextTTY *tty = &tty_data->tty.text;
    *out_revents = (ringbuffer_poll(&tty->backend.slave_rb) & POLLIN) | POLLOUT;
    return 0;
}

static DevOps g_text_tty_dev_ops = {.type = VNODE_TYPE_CHARDEV,
                                    .read = tty_read,
                                    .write = tty_write,
                                    .ioctl = tty_ioctl,
                                    .isatty = tty_isatty,
                                    .seekable = tty_seekable,
                                    .stat = tty_stat,
                                    .poll_head = tty_poll_head,
                                    .poll = tty_poll};

// TEXT Tty BACKEND INTERFACE

static bool is_ctty(Proc *proc, Vnode *tty_vn) {
    if (proc->proc_group == NULL || proc->proc_group->session == NULL) {
        return false;
    }

    // The tty may change concurrently, see TIOCSCTTY
    ProcSession *session = proc->proc_group->session;
    return tty_vn == __atomic_load_n(&session->tty, __ATOMIC_RELAXED);
}

int tty_text_backend_ioctl(TextTTYBackend *tty_backend, Vnode *tty_vn, u64 op,
                           void *buf) {
    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    if (op == TIOCGWINSZ) {
        struct winsize ws = {.ws_row = tty_backend->rows,
                             .ws_col = tty_backend->cols};
        return memcpy_to_user(buf, &ws, sizeof(ws));
    }

    if (op == FIONREAD) {
        int avail = ringbuffer_avail_read(&tty_backend->slave_rb);
        return memcpy_to_user(buf, &avail, sizeof(avail));
    }

    if (op == TCGETS) {
        return memcpy_to_user(buf, &tty_backend->termios,
                              sizeof(tty_backend->termios));
    }

    if (op == TCSETS || op == TCSETSF || op == TCSETSW) {
        struct termios termios;
        int ret = memcpy_from_user(&termios, buf, sizeof(termios));
        if (ret == 0) tty_backend->termios = termios;
        return ret;
    }

    if (op == TIOCGPGRP) {
        if (!is_ctty(curr_proc, tty_vn)) return ENOTTY;

        // POSIX says this must be an arbitrarly high number which is not
        // assigned to anyu process or process group
        _Static_assert(sizeof(pid_t) == sizeof(int), "pid_t is not an int");
        pid_t pgid = tty_backend->fg_pgid != 0 ? tty_backend->fg_pgid : INT_MAX;
        return memcpy_to_user(buf, &pgid, sizeof(pgid));
    }

    if (op == TIOCSCTTY) {
        if (curr_proc->proc_group == NULL ||
            curr_proc->proc_group->session == NULL) {
            return ENOTTY; // TODO: probably wrog errno
        }

        // The session owns a reference to its controlling terminal
        ProcSession *session = curr_proc->proc_group->session;
        vfs_vnode_hold(tty_vn);
        spinlock_acq(&session->pg_lock);
        Vnode *old = session->tty;
        __atomic_store_n(&session->tty, tty_vn, __ATOMIC_RELAXED);
        spinlock_rel(&session->pg_lock);
        vfs_vnode_rel(old);
        tty_backend->fg_pgid = curr_proc->proc_group->pgid;
        return 0;
    }

    if (op == TIOCSPGRP) {
        if (!is_ctty(curr_proc, tty_vn)) return ENOTTY;

        pid_t pgid;
        int ret = memcpy_from_user(&pgid, buf, sizeof(pgid));
        if (ret == 0) tty_backend->fg_pgid = pgid;
        return ret;
    }

    log_debug("unsupported ioctl 0x%X", op);
    return ENOSYS;
}

static int backend_echo(TextTTYBackend *tty_backend, const char *buf,
                        usize buflen, bool blocking, void *passthrough,
                        usize *out_bytes_written) {
    usize utmp;
    if (out_bytes_written == NULL) out_bytes_written = &utmp;

    tcflag_t oflag = tty_backend->termios.c_oflag;
    if (!(oflag & OPOST) || !(oflag & ONLCR)) {
        return tty_backend->echo(buf, buflen, blocking, passthrough,
                                 out_bytes_written);
    }

    // A newline only counts as written once all of its \r\n is
    usize count = 0;
    int e = 0;
    while (count < buflen) {
        const char *line = buf + count;
        const char *nl = memchr(line, '\n', buflen - count);
        usize len = nl != NULL ? (usize)(nl - line) : buflen - count;
        usize bw = 0;

        if (len != 0) {
            e = tty_backend->echo(line, len, blocking, passthrough, &bw);
            count += bw;
            if (e != 0 || bw < len) break;
        }
        if (nl == NULL) break;

        bw = 0;
        e = tty_backend->echo("\r\n", 2, blocking, passthrough, &bw);
        if (e != 0 || bw < 2) break;
        count++;
    }

    *out_bytes_written = count;
    return e;
}

// The backends may hold spinlocks, so they only ever see kernel memory
int tty_text_backend_echo(TextTTYBackend *tty_backend, const char *buf,
                          usize buflen, bool blocking, void *passthrough,
                          usize *out_bytes_written) {
    char chunk[128];
    usize done = 0;
    int ret = 0;

    while (done < buflen) {
        usize n = min(buflen - done, sizeof(chunk));
        usize w = 0;
        ret = memcpy_maybe_user(chunk, buf + done, n);
        if (ret != 0) break;
        ret = backend_echo(tty_backend, chunk, n, blocking, passthrough, &w);
        done += w;
        if (ret != 0 || w < n) break;
    }

    if (out_bytes_written != NULL) *out_bytes_written = done;

    return done != 0 ? 0 : ret;
}

void tty_text_backend_poll_callback(void *data) {
    TextTTYBackend *backend = data;

    spinlock_acq(&backend->slave_ph.lock);
    Polled *polled, *_;
    LIST_FOREACH_SAFE(polled, &backend->slave_ph.polled_list, polled_list, _) {
        poll_notify(polled->poller);
    }
    spinlock_rel(&backend->slave_ph.lock);
}

// CREDIT: vloxei64/ke
// (reworked somewhat now, will probably be reworked more soon)
int tty_process_char(TextTTYBackend *tty_backend, char c, bool blocking,
                     void *passthrough) {
    bool handled = false;
    bool line_done = false;
    int sig = SIGNAL_NONE;
    int ret = 0;

    if (tty_backend->termios.c_lflag & ISIG) {
        if (tty_backend->termios.c_cc[VINTR] == c) {
            ret = backend_echo(tty_backend, "^C", 2, blocking, passthrough,
                               NULL);
            sig = SIGINT;
            handled = true;
        }
        if (tty_backend->termios.c_cc[VQUIT] == c) {
            ret = backend_echo(tty_backend, "^\\\\", 3, blocking, passthrough,
                               NULL);
            sig = SIGQUIT;
            handled = true;
        }
        if (tty_backend->termios.c_cc[VSUSP] == c) {
            sig = SIGTSTP;
            handled = true;
        }
    }

    if (ret != 0) goto end;

    if (c == '\r' && tty_backend->termios.c_iflag & IGNCR) return 0;
    if (c == '\r' && tty_backend->termios.c_iflag & ICRNL) c = '\n';
    if (c == '\n' && tty_backend->termios.c_iflag & INLCR) c = '\r';

    // handle raw mode
    if (!(tty_backend->termios.c_lflag & ICANON)) {
        ret = ringbuffer_write(
            &tty_backend->slave_rb, &c, 1, RINGBUFFER_NOATOMIC, false,
            tty_text_backend_poll_callback, tty_backend, NULL, NULL);
        handled = true;
        goto end;
    }

    // handle canon mode

    if (tty_backend->termios.c_cc[VEOF] == c) {
        handled = true;
        line_done = true;

        if (tty_backend->canon.index == 0) tty_backend->slave_rb.is_eof = true;
    }

    if (c == '\n' || c == '\r') line_done = true;

    if (tty_backend->termios.c_cc[VERASE] == c) {
        if (tty_backend->canon.index > 0) {
            if (tty_backend->termios.c_lflag & ECHOE) {
                ret = backend_echo(tty_backend, "\b \b", 3, blocking,
                                   passthrough, NULL);
                if (ret != 0) goto end;
            }
            tty_backend->canon.index--;
        }

        handled = true;
    }

    // kill line
    if (tty_backend->termios.c_cc[VKILL] == c) {
        while (tty_backend->canon.index > 0) {
            if (tty_backend->canon.buffer[tty_backend->canon.index] == '\n') {
                break;
            }

            tty_backend->canon.index--;
            if (tty_backend->termios.c_lflag & ECHOKE) {
                ret = backend_echo(tty_backend, "\b \b", 3, blocking,
                                   passthrough, NULL);
                if (ret != 0) goto end;
            }
        }

        handled = true;
    }

end:
    if (!handled) {
        tty_backend->canon.buffer[tty_backend->canon.index++] = c;
        if (tty_backend->termios.c_lflag & ECHO) {
            ret = backend_echo(tty_backend, &c, 1, blocking, passthrough, NULL);
            if (ret != 0) goto sighandler;
        }
    }

    if (line_done) {
        usize nbytes = tty_backend->canon.index;
        ret = ringbuffer_write(
            &tty_backend->slave_rb, tty_backend->canon.buffer, nbytes,
            RINGBUFFER_NOATOMIC, blocking, tty_text_backend_poll_callback,
            tty_backend, NULL, NULL);
        tty_backend->canon.index = 0;
    }

sighandler:
    if (sig != SIGNAL_NONE) {
        signal_send_to_proc_group(tty_backend->fg_pgid, sig, NULL);
    }

    return ret;
}

int tty_process_chars(TextTTYBackend *tty_backend, const char *buf,
                      usize buflen, bool blocking, void *passthrough,
                      usize *out_bytes_processed) {
    int ret = 0;
    usize i = 0;
    char chunk[128];
    while (i < buflen) {
        usize n = min(buflen - i, sizeof(chunk));
        ret = memcpy_maybe_user(chunk, buf + i, n);
        if (ret != 0) break;

        usize j = 0;
        for (; j < n; j++) {
            ret = tty_process_char(tty_backend, chunk[j], blocking,
                                   passthrough);
            if (ret != 0) break;
        }

        i += j;
        if (ret != 0) break;
    }

    // If at least one char was written, there is no error
    if (i != 0) ret = 0;

    if (out_bytes_processed != NULL) *out_bytes_processed = i;

    return ret;
}

void tty_init_text_backend(TextTTYBackend *backend, usize rows, usize cols,
                           int (*echo)(const char *buf, usize buflen,
                                       bool blocking, void *passthrough,
                                       usize *out_bytes_written)) {
    memset(backend, 0, sizeof(TextTTYBackend));
    ringbuffer_init(&backend->slave_rb);
    sync_init_mutex(&backend->slave_rb.condvar);
    LIST_INIT(&backend->slave_ph.polled_list);
    backend->slave_ph.lock = SPINLOCK_NEW();
    backend->echo = echo;

    backend->rows = rows;
    backend->cols = cols;

    backend->termios.c_cc[VINTR] = CINTR;
    backend->termios.c_cc[VQUIT] = CQUIT;
    backend->termios.c_cc[VERASE] = CERASE;
    backend->termios.c_cc[VKILL] = CKILL;
    backend->termios.c_cc[VEOF] = CEOF;
    backend->termios.c_cc[VTIME] = CTIME;
    backend->termios.c_cc[VMIN] = CMIN;
    backend->termios.c_cc[VSWTC] = 0;
    backend->termios.c_cc[VSTART] = CSTART;
    backend->termios.c_cc[VSTOP] = CSTOP;
    backend->termios.c_cc[VSUSP] = CSUSP;
    backend->termios.c_cc[VEOL] = CEOL;
    backend->termios.c_cc[VREPRINT] = CREPRINT;
    backend->termios.c_cc[VDISCARD] = 0;
    backend->termios.c_cc[VWERASE] = CWERASE;
    backend->termios.c_cc[VLNEXT] = 0;
    backend->termios.c_cflag = TTYDEF_CFLAG;
    backend->termios.c_iflag = TTYDEF_IFLAG;
    backend->termios.c_lflag = TTYDEF_LFLAG;
    backend->termios.c_oflag = TTYDEF_OFLAG;
#ifdef __linux__
    backend->termios.c_ispeed = backend->termios.c_ospeed = TTYDEF_SPEED;
#else
    backend->termios.ibaud = backend->termios.obaud = TTYDEF_SPEED;
#endif
}

// KERNEL Tty INTERFACE

void tty_kbd_in(Tty *tty, KbdPacket *kbd_pkt) {
    mutex_acq(&tty->lock);
    if (tty->kbd_in != NULL) tty->kbd_in(tty, kbd_pkt);
    mutex_rel(&tty->lock);
}

void tty_mouse_in(Tty *tty, MousePacket *mouse_pkt) {
    mutex_acq(&tty->lock);
    if (tty->mouse_in != NULL) tty->mouse_in(tty, mouse_pkt);
    mutex_rel(&tty->lock);
}

void tty_enter_foreground(Tty *tty) {
    mutex_acq(&tty->lock);
    if (tty->type == TTY_TYPE_TEXT) {
        term_enable(tty->tty.text.term);
    } else if (ktty_is_process_switched(tty)) {
        ktty_signal_owner(tty, tty->tty.fb.switchmode.acqsig);
    }
    mutex_rel(&tty->lock);
}

void tty_leave_foreground(Tty *tty) {
    mutex_acq(&tty->lock);
    if (tty->type == TTY_TYPE_TEXT) term_disable(tty->tty.text.term);
    mutex_rel(&tty->lock);
}

bool tty_request_rel(Tty *tty) {
    mutex_acq(&tty->lock);
    bool wait = ktty_is_process_switched(tty) &&
        ktty_signal_owner(tty, tty->tty.fb.switchmode.relsig);
    mutex_rel(&tty->lock);
    return wait;
}

int tty_init_text(Term *term, Vnode **out_vnode, Tty **out_tty) {
    usize tty_num = __atomic_fetch_add(&g_next_tty_num, 1, __ATOMIC_SEQ_CST);
    log("initializing text tty %zu", tty_num);

    Tty *tty_data = slab_alloc(sizeof(Tty));
    mutex_init(&tty_data->lock);
    tty_data->type = TTY_TYPE_TEXT;
    tty_data->kbd_in = text_tty_kbd_in;
    tty_data->mouse_in = NULL;
    tty_data->tty.fb = (FBTty){0};
    tty_data->num = tty_num;
    TextTTY *tty = &tty_data->tty.text;
    tty->term = term;
    tty_data->panic_term = term;

    // Poor man's sprintf
    char tty_num_str[24];
    uitoa(tty_num, tty_num_str);
    char tty_name[3 + 24] = {0}; // "tty" + number
    strcpy(tty_name, "tty");
    strcpy(&tty_name[3], tty_num_str);

    Vnode *tty_dev;
    int ret = devfs_register(NULL, tty_name, strlen(tty_name),
                             &g_text_tty_dev_ops, tty_data, &tty_dev);

    if (ret != 0) {
        tty_dev = NULL;
        goto end;
    }

    tty_data->vnode = tty_dev;
    log_debug("tty vn=%X");

    usize rows, cols;
    term_get_size(term, &rows, &cols);
    tty_init_text_backend(&tty->backend, rows, cols, text_tty_echo);

end:
    if (out_vnode != NULL) *out_vnode = tty_dev;
    if (out_tty != NULL) *out_tty = tty_data;

    log_debug("created tty with name %s", tty_name);
    return ret;
}

// DEVTTY FUNCTIONS

static int devtty_open(void *devdata, Vnode **out_vnode) {
    (void)devdata;
    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    ProcSession *session = curr_proc->proc_group->session;
    spinlock_acq(&session->pg_lock);
    Vnode *ctty = session->tty;
    if (ctty != NULL) vfs_vnode_hold(ctty);
    spinlock_rel(&session->pg_lock);

    // An open of the controlling terminal, so it gets its own close
    *out_vnode = NULL;
    if (ctty == NULL) return ENXIO;

    int ret = vfs_open(ctty, out_vnode);
    vfs_vnode_rel(ctty);
    return ret;
}

static DevOps g_tty_clone_dev_ops = {.type = VNODE_TYPE_CHARDEV,
                                     .open = devtty_open,
                                     .stat = tty_stat};

int tty_devtty_init(Vnode **out_devtty) {
    log("initializing /dev/tty clone device");

    Vnode *dev = NULL;
    int ret = devfs_register(NULL, "tty", 3, &g_tty_clone_dev_ops, NULL, &dev);

    *out_devtty = dev;
    return ret;
}
