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

#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>
#include <lib/ringbuffer.h>

#define COM_IO_MOUSE_RIGHTBUTTON 0x01
#define COM_IO_MOUSE_MIDBUTTON   0x02
#define COM_IO_MOUSE_LEFTBUTTON  0x04
#define COM_IO_MOUSE_BUTTON4     0x08
#define COM_IO_MOUSE_BUTTON5     0x10

typedef struct com_mouse_packet {
    struct com_mouse *mouse;
    int               buttons;
    int               dx;
    int               dy;
    int               dz;
} com_mouse_packet_t;

typedef struct com_mouse {
    com_vnode_t         *vnode;
    kringbuffer_t        rb;
    struct com_poll_head pollhead;

    // Packets are first written to the ringbuffer, but since that is only
    // ARCH_PAGE_SIZE / 4, it will fill up very quickly. Thus, all other packets
    // are "consolidated" into a single one. The reason why this is not the
    // default (which would be much faster in terms of overhead) is that this
    // would apparently break mouse acceleration in userspace. Thus we try to
    // strike a balance between performance, precision, and features
    struct {
        com_mouse_packet_t packet;
        bool               present;
    } consolidated;
} com_mouse_t;

com_mouse_t *com_io_mouse_new(void);
int com_io_mouse_send_packet(com_mouse_t *mouse, com_mouse_packet_t *pkt);
