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
#include <lib/ringbuffer.h>
#include <lib/str.h>
#include <lib/util.h>
#include <lib/workqueue.h>
#include <salernos/ktty.h>
#include <stdint.h>
#include <vendor/tailq.h>

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

static com_tty_t *Ttys[CONFIG_TTY_MAX] = {NULL};
static int        ForegroundTtyIdx     = 0;
static int        TtyNext              = 0;

static kringbuffer_t KbdRb;
static kworkqueue_t  KbdQueue;
static kwork_t       KbdWork;

static kringbuffer_t MouseRb;
static kworkqueue_t  MouseQueue;
static kwork_t       MouseWork;

static void switch_tty(int new_fg) {
    if (new_fg >= CONFIG_TTY_MAX) {
        return;
    }

    com_tty_t
        *fg_tty = Ttys[__atomic_load_n(&ForegroundTtyIdx, __ATOMIC_SEQ_CST)];
    __atomic_store_n(&ForegroundTtyIdx, new_fg, __ATOMIC_SEQ_CST);

    if (NULL != fg_tty) {
        if (E_COM_TTY_TYPE_TEXT == fg_tty->type) {
            com_io_term_disable(fg_tty->tty.text.term);
        }
    }

    com_tty_t *new_tty = Ttys[new_fg];

    if (NULL != new_tty) {
        if (E_COM_TTY_TYPE_TEXT == new_tty->type) {
            com_io_term_enable(new_tty->tty.text.term);
        }
    }
}

static void kbd_dispatch(com_kbd_packet_t *pkt) {
    com_keyboard_t *keyboard = pkt->keyboard;

    if (COM_IO_KEYBOARD_MOD_SHIFT & keyboard->state.mod &&
        pkt->keycode >= COM_IO_KEYBOARD_KEY_F1 &&
        pkt->keycode <= COM_IO_KEYBOARD_KEY_F12) {
        size_t new_fg = pkt->keycode - COM_IO_KEYBOARD_KEY_F1;
        KDEBUG("switching to tty %d", new_fg);
        switch_tty(new_fg);
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
    int tty_idx = __atomic_fetch_add(&TtyNext, 1, __ATOMIC_SEQ_CST);
    KDEBUG("adding tty %d to kernel console", tty_idx);
    KASSERT(tty_idx < CONFIG_TTY_MAX);
    Ttys[tty_idx] = tty;
}

void com_io_console_init(void) {
    KRINGBUFFER_INIT(&KbdRb);
    KWORK_INIT(&KbdWork, kbd_work);
    kworkqueue_init(&KbdQueue);

    KRINGBUFFER_INIT(&MouseRb);
    KWORK_INIT(&MouseWork, mouse_work);
    kworkqueue_init(&MouseQueue);
}
