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

#include <kernel/com/io/console.h>
#include <kernel/com/io/term.h>
#include <lib/util.h>
#include <stdint.h>
#include <vendor/tailq.h>

static com_tty_t *Ttys[CONFIG_TTY_MAX] = {NULL};
static int        ForegroundTtyIdx     = 0;
static int        TtyNext              = 0;

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

void com_io_console_kbd_in(com_kbd_packet_t *pkt) {
    static int mod = 0;

    if (COM_IO_KEYBOARD_KEYSTATE_RELEASED == pkt->keystate) {
        switch (pkt->keycode) {
            case COM_IO_KEYBOARD_KEY_LEFTCTRL:
                mod &= ~(COM_IO_TTY_MOD_LCTRL);
                break;
            case COM_IO_KEYBOARD_KEY_RIGHTCTRL:
                mod &= ~(COM_IO_TTY_MOD_RCTRL);
                break;
            case COM_IO_KEYBOARD_KEY_LEFTALT:
                mod &= ~(COM_IO_TTY_MOD_LALT);
                break;
            case COM_IO_KEYBOARD_KEY_RIGHTALT:
                mod &= ~(COM_IO_TTY_MOD_RALT);
                break;
            case COM_IO_KEYBOARD_KEY_LEFTSHIFT:
                mod &= ~(COM_IO_TTY_MOD_LSHIFT);
                break;
            case COM_IO_KEYBOARD_KEY_RIGHTSHIFT:
                mod &= ~(COM_IO_TTY_MOD_RSHIFT);
                break;
            default:
                break;
        }
    }

    // Here COM_IO_KEYBOARD_KEYSTATE_PRESSED == pkt->keystate
    switch (pkt->keycode) {
        // Modifiers
        case COM_IO_KEYBOARD_KEY_LEFTSHIFT:
            mod = mod | COM_IO_TTY_MOD_LSHIFT;
            break;
        case COM_IO_KEYBOARD_KEY_RIGHTSHIFT:
            mod = mod | COM_IO_TTY_MOD_RSHIFT;
            break;
        case COM_IO_KEYBOARD_KEY_LEFTCTRL:
            mod = mod | COM_IO_TTY_MOD_LCTRL;
            break;
        case COM_IO_KEYBOARD_KEY_RIGHTCTRL:
            mod = mod | COM_IO_TTY_MOD_RCTRL;
            break;
        case COM_IO_KEYBOARD_KEY_LEFTALT:
            mod = mod | COM_IO_TTY_MOD_LALT;
            break;
        case COM_IO_KEYBOARD_KEY_RIGHTALT:
            mod = mod | COM_IO_TTY_MOD_RALT;
            break;

        default:
            break;
    }

    if ((COM_IO_TTY_MOD_LSHIFT | COM_IO_TTY_MOD_RSHIFT) & mod &&
        pkt->keycode >= COM_IO_KEYBOARD_KEY_F1 &&
        pkt->keycode <= COM_IO_KEYBOARD_KEY_F12) {
        size_t new_fg = pkt->keycode - COM_IO_KEYBOARD_KEY_F1;
        KDEBUG("switching to tty %d", new_fg);
        switch_tty(new_fg);
        mod = 0;
        return;
    }

    com_tty_t
        *fg_tty = Ttys[__atomic_load_n(&ForegroundTtyIdx, __ATOMIC_SEQ_CST)];

    if (NULL != fg_tty) {
        com_io_tty_kbd_in(fg_tty, pkt);
    }
}

void com_io_console_mouse_in(com_mouse_packet_t *pkt) {
    com_tty_t
        *fg_tty = Ttys[__atomic_load_n(&ForegroundTtyIdx, __ATOMIC_SEQ_CST)];

    if (NULL != fg_tty) {
        com_io_tty_mouse_in(fg_tty, pkt);
    }
}

void com_io_console_add_tty(com_tty_t *tty) {
    int tty_idx = __atomic_fetch_add(&TtyNext, 1, __ATOMIC_SEQ_CST);
    KDEBUG("adding tty %d to kernel console", tty_idx);
    KASSERT(tty_idx < CONFIG_TTY_MAX);
    Ttys[tty_idx] = tty;
}

void com_io_console_init(void) {
    // Nothing for now, not needed
}
