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
#include <lib/ringbuffer.h>
#include <stdint.h>
#include <termios.h>

#define COM_IO_TTY_BUF_SZ (ARCH_PAGE_SIZE / 4)

#define COM_IO_TTY_MOD_LCTRL  1UL
#define COM_IO_TTY_MOD_RCTRL  2UL
#define COM_IO_TTY_MOD_ARROW  4UL
#define COM_IO_TTY_MOD_LSHIFT 8UL
#define COM_IO_TTY_MOD_RSHIFT 16UL
#define COM_IO_TTY_MOD_LALT   32UL
#define COM_IO_TTY_MOD_RALT   64UL
#define COM_IO_TTY_MOD_FKEY   128UL

typedef enum com_tty_type { COM_TTY_TEXT, COM_TTY_FRAMEBUFFER } com_tty_type_t;

// This is shared by both PTY and TTY
typedef struct com_text_tty_backend {
    size_t         rows;
    size_t         cols;
    struct termios termios;
    pid_t          fg_pgid;
    kringbuffer_t  slave_rb; // this is called slave for familiarity with PTYs
                             // (even if it used by TTYs as well)
    struct {
        char   buffer[COM_IO_TTY_BUF_SZ];
        size_t index;
    } canon;

    size_t (*echo)(const char *buf,
                   size_t      buflen,
                   bool        blocking,
                   void       *passthrough);
} com_text_tty_backend_t;

typedef struct com_text_tty {
    com_term_t            *term;
    com_text_tty_backend_t backend;
} com_text_tty_t;

typedef struct com_fb_tty {

} com_fb_tty_t;

typedef struct com_tty {
    com_tty_type_t type;
    com_vnode_t   *vnode;
    union {
        com_text_tty_t text;
        com_fb_tty_t   fb;
    } tty;
    void (*kbd_in)(struct com_tty *tty, char c, uintmax_t mod);
    size_t num;
} com_tty_t;

int  com_io_tty_text_backend_ioctl(com_text_tty_backend_t *tty_backend,
                                   com_vnode_t            *tty_vn,
                                   uintmax_t               op,
                                   void                   *buf);
void com_io_tty_process_char(com_text_tty_backend_t *tty_backend,
                             char                    c,
                             bool                    blocking,
                             void                   *passthrough);
void com_io_tty_process_chars(com_text_tty_backend_t *tty_backend,
                              const char             *buf,
                              size_t                  buflen,
                              bool                    blocking,
                              void                   *passthrough);
void com_io_tty_kbd_in(com_tty_t *tty, char c, uintmax_t mod);
void com_io_tty_init_text_backend(com_text_tty_backend_t *backend,
                                  size_t                  rows,
                                  size_t                  cols,
                                  size_t (*echo)(const char *buf,
                                                 size_t      buflen,
                                                 bool        blocking,
                                                 void       *passthrough));
int  com_io_tty_init_text(com_vnode_t **out,
                          com_tty_t   **out_tty,
                          com_term_t   *term);
