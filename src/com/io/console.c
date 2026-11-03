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

#include <fcntl.h>
#include <kernel/com/io/console.h>
#include <kernel/com/io/term.h>
#include <kernel/com/sys/sched.h>
#include <lib/mutex.h>
#include <lib/ringbuffer.h>
#include <lib/str.h>
#include <lib/util.h>
#include <lib/workqueue.h>
#include <salernos/ktty.h>
#include <stdint.h>
#include <vendor/tailq.h>

#define NO_PENDING_SWITCH ((size_t)-1)

struct console_kbd_packet {
    com_keyboard_t *keyboard;
    uint8_t         keycode;
    uint8_t         keystate;
} __attribute__((packed));

struct console_mouse_packet {
    com_mouse_t *mouse;
    uint8_t      buttons;
    int16_t      dx;
    int16_t      dy;
    int8_t       dz;
} __attribute__((packed));

// NOTE: the TTY switch protocol uses a two-phase approach for TTYs managed by
// userspace processes in order to avoid awaiting user progress (which is
// impossible to guarantee). Therefore, every switch first checks if needs to
// "wait": if it does, the switch-in TTY ID is stored as pending and all locks
// are dropped. This creates a state machine that then allows the system
// implemented in tty.c to ACK or NACK the switch. If a TTY is not managed by a
// user process, the two-phase protocol is never used because a ait is never
// required

static com_tty_t     *Ttys[CONFIG_TTY_MAX] = {NULL};
static size_t         TtyNext              = 0;
static size_t         ForegroundTtyIdx     = 0;
static size_t         PendingTtyIdx        = NO_PENDING_SWITCH;
static kmutex_t       SwitchLock;
static com_waitlist_t ForegroundWaitlist;

static kringbuffer_t KbdRb;
static kworkqueue_t  KbdQueue;
static kwork_t       KbdWork;

static kringbuffer_t MouseRb;
static kworkqueue_t  MouseQueue;
static kwork_t       MouseWork;

static void set_foreground_nolock(size_t new_fg) {
    com_tty_t *old_tty = Ttys[ForegroundTtyIdx];
    com_tty_t *new_tty = Ttys[new_fg];

    PendingTtyIdx = NO_PENDING_SWITCH;
    __atomic_store_n(&ForegroundTtyIdx, new_fg, __ATOMIC_SEQ_CST);
    com_io_tty_leave_foreground(old_tty);
    com_io_tty_enter_foreground(new_tty);
    com_sys_sched_notify_all(&ForegroundWaitlist);
}

static void switch_nolock(size_t new_fg) {
    if (new_fg == ForegroundTtyIdx) {
        PendingTtyIdx = NO_PENDING_SWITCH;
        return;
    }

    // Phase 1: if waiting is required, we set the ID as pending
    if (com_io_tty_request_release(Ttys[ForegroundTtyIdx])) {
        PendingTtyIdx = new_fg;
        return;
    }

    // Phase 2: when waiting is not/no-longer required (kernel-managed TTY, or
    // user ACK arrived), we perform the actual switch
    set_foreground_nolock(new_fg);
}

static void kbd_dispatch(com_kbd_packet_t *pkt) {
    com_keyboard_t *keyboard = pkt->keyboard;

    if (COM_IO_KEYBOARD_MOD_SHIFT & keyboard->state.mod &&
        pkt->keycode >= COM_IO_KEYBOARD_KEY_F1 &&
        pkt->keycode <= COM_IO_KEYBOARD_KEY_F12) {
        size_t new_fg = pkt->keycode - COM_IO_KEYBOARD_KEY_F1;
        KDEBUG("switching to tty %zu", new_fg);
        com_io_console_switch(new_fg);
        return;
    }

    com_tty_t
        *fg_tty = Ttys[__atomic_load_n(&ForegroundTtyIdx, __ATOMIC_SEQ_CST)];

    if (NULL != fg_tty) {
        com_io_tty_kbd_in(fg_tty, pkt);
    }
}

static void mouse_dispatch(com_mouse_packet_t *pkt) {
    com_tty_t
        *fg_tty = Ttys[__atomic_load_n(&ForegroundTtyIdx, __ATOMIC_SEQ_CST)];

    if (NULL != fg_tty) {
        com_io_tty_mouse_in(fg_tty, pkt);
    }
}

static void kbd_work(kwork_t *work) {
    (void)work;
    struct console_kbd_packet packed;

    while (0 == kringbuffer_read(&packed,
                                 NULL,
                                 &KbdRb,
                                 sizeof(packed),
                                 sizeof(packed),
                                 false,
                                 NULL,
                                 NULL,
                                 NULL)) {
        com_kbd_packet_t pkt = {.keyboard = packed.keyboard,
                                .keycode  = packed.keycode,
                                .keystate = packed.keystate};
        if (E_COM_KBD_ACTION_SEND != pkt.keyboard->layout(&pkt)) {
            continue;
        }

        com_io_keyboard_update_state(&pkt);
        kbd_dispatch(&pkt);
    }
}

static void mouse_work(kwork_t *work) {
    (void)work;
    struct console_mouse_packet packed;

    while (0 == kringbuffer_read(&packed,
                                 NULL,
                                 &MouseRb,
                                 sizeof(packed),
                                 sizeof(packed),
                                 false,
                                 NULL,
                                 NULL,
                                 NULL)) {
        com_mouse_packet_t pkt = {.mouse   = packed.mouse,
                                  .buttons = packed.buttons,
                                  .dx      = packed.dx,
                                  .dy      = packed.dy,
                                  .dz      = packed.dz};
        mouse_dispatch(&pkt);
    }
}

void com_io_console_kbd_in(com_kbd_packet_t *pkt) {
    struct console_kbd_packet packed = {.keyboard = pkt->keyboard,
                                        .keycode  = pkt->keycode,
                                        .keystate = pkt->keystate};
    kringbuffer_write(NULL,
                      &KbdRb,
                      &packed,
                      sizeof(packed),
                      sizeof(packed),
                      false,
                      NULL,
                      NULL,
                      NULL);
    kworkqueue_push(&KbdQueue, &KbdWork);
}

void com_io_console_mouse_in(com_mouse_packet_t *pkt) {
    struct console_mouse_packet packed = {.mouse   = pkt->mouse,
                                          .buttons = pkt->buttons,
                                          .dx      = pkt->dx,
                                          .dy      = pkt->dy,
                                          .dz      = pkt->dz};
    kringbuffer_write(NULL,
                      &MouseRb,
                      &packed,
                      sizeof(packed),
                      sizeof(packed),
                      false,
                      NULL,
                      NULL,
                      NULL);
    kworkqueue_push(&MouseQueue, &MouseWork);
}

void com_io_console_add_tty(com_tty_t *tty) {
    size_t tty_idx = __atomic_fetch_add(&TtyNext, 1, __ATOMIC_SEQ_CST);
    KDEBUG("adding tty %zu to kernel console", tty_idx);
    KASSERT(tty_idx < CONFIG_TTY_MAX);
    Ttys[tty_idx] = tty;
}

bool com_io_console_switch(size_t tty_idx) {
    if (tty_idx >= CONFIG_TTY_MAX || NULL == Ttys[tty_idx]) {
        return false;
    }

    kmutex_acquire(&SwitchLock);
    switch_nolock(tty_idx);
    kmutex_release(&SwitchLock);
    return true;
}

bool com_io_console_release(com_tty_t *tty, bool allow) {
    bool answered = false;
    kmutex_acquire(&SwitchLock);

    if (tty == Ttys[ForegroundTtyIdx] && NO_PENDING_SWITCH != PendingTtyIdx) {
        answered = true;
        if (allow) {
            set_foreground_nolock(PendingTtyIdx);
        } else {
            PendingTtyIdx = NO_PENDING_SWITCH;
        }
    }

    kmutex_release(&SwitchLock);
    return answered;
}

// NOTE: this is lockless so that TTYs can call it while holding their lock.
// This is safe because the foreground index is updated before calling
// com_io_tty_enter/leave_foreground, which also take the TTY lock
bool com_io_console_is_foreground(com_tty_t *tty) {
    return tty == Ttys[__atomic_load_n(&ForegroundTtyIdx, __ATOMIC_SEQ_CST)];
}

bool com_io_console_wait_foreground(com_tty_t *tty) {
    bool interrupted = false;
    kmutex_acquire(&SwitchLock);

    while (!interrupted && tty != Ttys[ForegroundTtyIdx]) {
        interrupted = 0 !=
                      kmutex_wait(&SwitchLock, &ForegroundWaitlist, 0, true);
    }

    kmutex_release(&SwitchLock);
    return !interrupted;
}

void com_io_console_init(void) {
    KMUTEX_INIT(&SwitchLock);
    COM_SYS_THREAD_WAITLIST_INIT(&ForegroundWaitlist);

    KRINGBUFFER_INIT(&KbdRb);
    KWORK_INIT(&KbdWork, kbd_work);
    kworkqueue_init(&KbdQueue);

    KRINGBUFFER_INIT(&MouseRb);
    KWORK_INIT(&MouseWork, mouse_work);
    kworkqueue_init(&MouseQueue);
}
