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

static com_tty_t *Ttys[COM_IO_CONSOLE_MAX_TTYS] = {NULL};
static int        ForegroundTtyIdx              = 0;
static int        TtyNext                       = 0;

static void switch_tty(int new_fg) {
    if (new_fg >= COM_IO_CONSOLE_MAX_TTYS) {
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

void com_io_console_kbd_in(char c, uintmax_t mod) {
    if (COM_IO_TTY_MOD_FKEY & mod &&
        /* COM_IO_TTY_MOD_LCTRL & mod && */ (COM_IO_TTY_MOD_LSHIFT |
                                             COM_IO_TTY_MOD_RSHIFT) &
            mod) {
        size_t new_fg = c - 'A';
        KDEBUG("switching to tty %d", new_fg);
        switch_tty(new_fg);
        return;
    }

    com_tty_t
        *fg_tty = Ttys[__atomic_load_n(&ForegroundTtyIdx, __ATOMIC_SEQ_CST)];

    if (NULL != fg_tty) {
        com_io_tty_kbd_in(fg_tty, c, mod);
    }
}

void com_io_console_add_tty(com_tty_t *tty) {
    int tty_idx = __atomic_fetch_add(&TtyNext, 1, __ATOMIC_SEQ_CST);
    KDEBUG("adding tty %d to kernel console", tty_idx);
    KASSERT(tty_idx < COM_IO_CONSOLE_MAX_TTYS);
    Ttys[tty_idx] = tty;
}

void com_io_console_init(void) {
    // Nothing for now, not needed
}
