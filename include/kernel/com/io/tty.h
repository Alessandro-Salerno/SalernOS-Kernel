/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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

#include <stdint.h>

#define COM_IO_TTY_MOD_LCTRL  1UL
#define COM_IO_TTY_MOD_RCTRL  2UL
#define COM_IO_TTY_MOD_ARROW  4UL
#define COM_IO_TTY_MOD_LSHIFT 8UL
#define COM_IO_TTY_MOD_RSHIFT 16UL
#define COM_IO_TTY_MOD_LALT   32UL
#define COM_IO_TTY_MOD_RALT   64UL

void com_io_tty_kbd_in(char c, uintmax_t mod);
int  com_io_tty_init(void);
