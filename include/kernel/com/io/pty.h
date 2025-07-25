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

#include <arch/info.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/thread.h>
#include <lib/ringbuffer.h>
#include <stddef.h>
#include <stdint.h>
#include <termios.h>

typedef struct com_pty {
    com_text_tty_backend_t data;

    com_vnode_t  *master_vn;
    kringbuffer_t master_rb;

    com_vnode_t *slave_vn;
    // no slave_rb since the TTY backend already has it
} com_pty_t;
