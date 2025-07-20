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

#pragma once

#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/term.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/thread.h>
#include <stdint.h>
#include <termios.h>

#define COM_IO_TTY_MOD_LCTRL  1UL
#define COM_IO_TTY_MOD_RCTRL  2UL
#define COM_IO_TTY_MOD_ARROW  4UL
#define COM_IO_TTY_MOD_LSHIFT 8UL
#define COM_IO_TTY_MOD_RSHIFT 16UL
#define COM_IO_TTY_MOD_LALT   32UL
#define COM_IO_TTY_MOD_RALT   64UL
#define COM_IO_TTY_MOD_FKEY   128UL

typedef enum com_tty_type { COM_TTY_TEXT, COM_TTY_FRAMEBUFFER } com_tty_type_t;

typedef struct com_text_tty {
    com_term_t             *term;
    size_t                  rows;
    size_t                  cols;
    struct termios          termios;
    com_spinlock_t          lock;
    com_spinlock_t          write_lock;
    struct com_thread_tailq waitlist;
    uint8_t                 buf[2048];
    size_t                  write;
} com_text_tty_t;

typedef struct com_fb_tty {

} com_fb_tty_t;

typedef struct com_tty {
    com_tty_type_t type;
    union {
        com_text_tty_t text;
        com_fb_tty_t   fb;
    } tty;
    void (*kbd_in)(struct com_tty *tty, char c, uintmax_t mod);
    size_t num;
} com_tty_t;

void com_io_tty_kbd_in(com_tty_t *tty, char c, uintmax_t mod);
int  com_io_tty_init_text(com_vnode_t **out,
                          com_tty_t   **out_tty,
                          com_term_t   *term);
