#pragma once

#include <kernel/com/ds/ringbuffer.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/keyboard.h>
#include <kernel/com/io/mouse.h>
#include <kernel/com/io/term.h>
#include <kernel/com/sync/mutex.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <salernos/ktty.h>
#include <termios.h>

#define TTY_BUF_SZ (PAGE_SIZE / 4)

#define TTY_MOD_ARROW 256

typedef enum TTYType {
    TTY_TYPE_TEXT,
    TTY_TYPE_FRAMEBUFFER
} TTYType;

// This is shared by both Pty and Tty
typedef struct TextTTYBackend {
    usize rows;
    usize cols;
    struct termios termios;
    pid_t fg_pgid;
    Ringbuffer slave_rb; // this is called slave for familiarity with PTYs
                         // (even if it used by TTYs as well)
    PollHead slave_ph;
    struct {
        char buffer[TTY_BUF_SZ];
        usize index;
    } canon;

    int (*echo)(const char *buf, usize buflen, bool blocking, void *passthrough,
                usize *out_bytes_written);
} TextTTYBackend;

typedef struct TextTTY {
    Term *term;
    TextTTYBackend backend;
} TextTTY;

typedef struct FBTty {
    pid_t owner;
    struct ktty_switchmode switchmode;
} FBTty;

typedef struct Tty {
    Mutex lock; // type and dependent fields
    TTYType type;
    Vnode *vnode;

    Term *panic_term;

    struct {
        TextTTY text;
        FBTty fb;
    } tty;
    void (*kbd_in)(struct Tty *tty, KbdPacket *kbd_pkt);
    void (*mouse_in)(struct Tty *tty, MousePacket *kbd_pkt);
    usize num;
} Tty;

int tty_text_backend_ioctl(TextTTYBackend *tty_backend, Vnode *tty_vn, u64 op,
                           void *buf);
int tty_text_backend_echo(TextTTYBackend *tty_backend, const char *buf,
                          usize buflen, bool blocking, void *passthrough,
                          usize *out_bytes_written);
void tty_text_backend_poll_callback(void *data);
int tty_process_char(TextTTYBackend *tty_backend, char c, bool blocking,
                     void *passthrough);
int tty_process_chars(TextTTYBackend *tty_backend, const char *buf,
                      usize buflen, bool blocking, void *passthrough,
                      usize *out_bytes_processed);
void tty_init_text_backend(TextTTYBackend *backend, usize rows, usize cols,
                           int (*echo)(const char *buf, usize buflen,
                                       bool blocking, void *passthrough,
                                       usize *out_bytes_written));

void tty_kbd_in(Tty *tty, KbdPacket *kbd_pkt);
void tty_mouse_in(Tty *tty, MousePacket *mouse_pkt);
void tty_enter_foreground(Tty *tty);
void tty_leave_foreground(Tty *tty);
bool tty_request_rel(Tty *tty);
int tty_init_text(Term *term, Vnode **out_vnode, Tty **out_tty);

int tty_devtty_init(Vnode **out_devtty);
