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

#pragma once

#include <kernel/com/io/keyboard.h>
#include <kernel/com/io/mouse.h>
#include <kernel/com/io/tty.h>
#include <stdint.h>

void com_io_console_kbd_in(com_kbd_packet_t *pkt);
void com_io_console_mouse_in(com_mouse_packet_t *pkt);
void com_io_console_add_tty(com_tty_t *tty);
void com_io_console_init(void);
