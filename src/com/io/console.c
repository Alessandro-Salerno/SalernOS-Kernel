#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/ds/ringbuffer.h>
#include <kernel/com/io/console.h>
#include <kernel/com/io/term.h>
#include <kernel/com/sync/mutex.h>
#include <kernel/com/sync/workqueue.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <salernos/ktty.h>
#include <vendor/tailq.h>

#define NO_PENDING_SWITCH ((usize) - 1)

struct ConsoleKbdPacket {
    Keyboard *keyboard;
    u8 keycode;
    u8 keystate;
} packed;

struct ConsoleMousePacket {
    Mouse *mouse;
    u8 buttons;
    i16 dx;
    i16 dy;
    i8 dz;
} packed;

// NOTE: the Tty switch protocol uses a two-phase approach for TTYs managed by
// userspace processes in order to avoid awaiting user progress (which is
// impossible to guarantee). Therefore, every switch first checks if needs to
// "wait": if it does, the switch-in Tty ID is stored as pending and all locks
// are dropped. This creates a state machine that then allows the system
// implemented in tty.c to ACK or NACK the switch. If a Tty is not managed by a
// user process, the two-phase protocol is never used because a ait is never
// required

static Tty *g_ttys[CONFIG_TTY_MAX] = {NULL};
static usize g_tty_next = 0;
static usize g_foreground_tty_idx = 0;
static usize g_pending_tty_idx = NO_PENDING_SWITCH;
static Mutex g_switch_lock;
static Waitlist g_foreground_waitlist;

static Ringbuffer g_kbd_rb;
static WorkQueue g_kbd_queue;
static Work g_kbd_work;

static Ringbuffer g_mouse_rb;
static WorkQueue g_mouse_queue;
static Work g_mouse_work;

static void set_foreground_nolock(usize new_fg) {
    Tty *old_tty = g_ttys[g_foreground_tty_idx];
    Tty *new_tty = g_ttys[new_fg];

    g_pending_tty_idx = NO_PENDING_SWITCH;
    __atomic_store_n(&g_foreground_tty_idx, new_fg, __ATOMIC_SEQ_CST);
    tty_leave_foreground(old_tty);
    tty_enter_foreground(new_tty);
    sched_notify_all(&g_foreground_waitlist);
}

static void switch_nolock(usize new_fg) {
    if (new_fg == g_foreground_tty_idx) {
        g_pending_tty_idx = NO_PENDING_SWITCH;
        return;
    }

    // Phase 1: if waiting is required, we set the ID as pending
    if (tty_request_rel(g_ttys[g_foreground_tty_idx])) {
        g_pending_tty_idx = new_fg;
        return;
    }

    // Phase 2: when waiting is not/no-longer required (kernel-managed Tty, or
    // user ACK arrived), we perform the actual switch
    set_foreground_nolock(new_fg);
}

static void kbd_dispatch(KbdPacket *pkt) {
    Keyboard *keyboard = pkt->keyboard;

    if (keyboard->state.mod & KEYBOARD_MOD_SHIFT &&
        pkt->keycode >= KEYBOARD_KEY_F1 && pkt->keycode <= KEYBOARD_KEY_F12) {
        usize new_fg = pkt->keycode - KEYBOARD_KEY_F1;
        log_debug("switching to tty %zu", new_fg);
        console_switch(new_fg);
        return;
    }

    Tty *fg_tty = g_ttys[__atomic_load_n(&g_foreground_tty_idx,
                                         __ATOMIC_SEQ_CST)];

    if (fg_tty != NULL) tty_kbd_in(fg_tty, pkt);
}

static void mouse_dispatch(MousePacket *pkt) {
    Tty *fg_tty = g_ttys[__atomic_load_n(&g_foreground_tty_idx,
                                         __ATOMIC_SEQ_CST)];

    if (fg_tty != NULL) tty_mouse_in(fg_tty, pkt);
}

static void kbd_work(Work *work) {
    (void)work;
    struct ConsoleKbdPacket raw;

    while (ringbuffer_read(&raw, &g_kbd_rb, sizeof(raw), sizeof(raw), false,
                           NULL, NULL, NULL, NULL) == 0) {
        KbdPacket pkt = {.keyboard = raw.keyboard,
                         .keycode = raw.keycode,
                         .keystate = raw.keystate};
        if (pkt.keyboard->layout(&pkt) != KBD_ACTION_SEND) continue;

        keyboard_update_state(&pkt);
        kbd_dispatch(&pkt);
    }
}

static void mouse_work(Work *work) {
    (void)work;
    struct ConsoleMousePacket raw;

    while (ringbuffer_read(&raw, &g_mouse_rb, sizeof(raw), sizeof(raw), false,
                           NULL, NULL, NULL, NULL) == 0) {
        MousePacket pkt = {.mouse = raw.mouse,
                           .buttons = raw.buttons,
                           .dx = raw.dx,
                           .dy = raw.dy,
                           .dz = raw.dz};
        mouse_dispatch(&pkt);
    }
}

void console_kbd_in(KbdPacket *pkt) {
    struct ConsoleKbdPacket raw = {.keyboard = pkt->keyboard,
                                   .keycode = pkt->keycode,
                                   .keystate = pkt->keystate};
    ringbuffer_write(&g_kbd_rb, &raw, sizeof(raw), sizeof(raw), false, NULL,
                     NULL, NULL, NULL);
    workqueue_push(&g_kbd_queue, &g_kbd_work);
}

void console_mouse_in(MousePacket *pkt) {
    struct ConsoleMousePacket raw = {.mouse = pkt->mouse,
                                     .buttons = pkt->buttons,
                                     .dx = pkt->dx,
                                     .dy = pkt->dy,
                                     .dz = pkt->dz};
    ringbuffer_write(&g_mouse_rb, &raw, sizeof(raw), sizeof(raw), false, NULL,
                     NULL, NULL, NULL);
    workqueue_push(&g_mouse_queue, &g_mouse_work);
}

void console_add_tty(Tty *tty) {
    usize tty_idx = __atomic_fetch_add(&g_tty_next, 1, __ATOMIC_SEQ_CST);
    log_debug("adding tty %zu to kernel console", tty_idx);
    assert(tty_idx < CONFIG_TTY_MAX);
    g_ttys[tty_idx] = tty;
}

bool console_switch(usize tty_idx) {
    if (tty_idx >= CONFIG_TTY_MAX || g_ttys[tty_idx] == NULL) return false;

    mutex_acq(&g_switch_lock);
    switch_nolock(tty_idx);
    mutex_rel(&g_switch_lock);
    return true;
}

bool console_rel(Tty *tty, bool allow) {
    bool answered = false;
    mutex_acq(&g_switch_lock);

    if (tty == g_ttys[g_foreground_tty_idx] &&
        g_pending_tty_idx != NO_PENDING_SWITCH) {
        answered = true;
        if (allow) set_foreground_nolock(g_pending_tty_idx);
        else g_pending_tty_idx = NO_PENDING_SWITCH;
    }

    mutex_rel(&g_switch_lock);
    return answered;
}

// NOTE: this is lockless so that TTYs can call it while holding their lock.
// This is safe because the foreground index is updated before calling
// tty_enter/leave_foreground, which also take the Tty lock
bool console_is_foreground(Tty *tty) {
    return tty ==
        g_ttys[__atomic_load_n(&g_foreground_tty_idx, __ATOMIC_SEQ_CST)];
}

bool console_wait_foreground(Tty *tty) {
    bool interrupted = false;
    mutex_acq(&g_switch_lock);

    while (!interrupted && tty != g_ttys[g_foreground_tty_idx]) {
        interrupted = mutex_wait(&g_switch_lock, &g_foreground_waitlist, 0,
                                 true) != 0;
    }

    mutex_rel(&g_switch_lock);
    return !interrupted;
}

void console_init(void) {
    mutex_init(&g_switch_lock);
    waitlist_init(&g_foreground_waitlist);

    ringbuffer_init(&g_kbd_rb);
    work_init(&g_kbd_work, kbd_work);
    workqueue_init(&g_kbd_queue, NULL);

    ringbuffer_init(&g_mouse_rb);
    work_init(&g_mouse_work, mouse_work);
    workqueue_init(&g_mouse_queue, NULL);
}
