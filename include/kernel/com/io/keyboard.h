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

#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>
#include <lib/ringbuffer.h>

// TAKEN: Mathewnd/Astral
#define COM_IO_KEYBOARD_KEY_RESERVED       0
#define COM_IO_KEYBOARD_KEY_ESCAPE         1
#define COM_IO_KEYBOARD_KEY_1              2
#define COM_IO_KEYBOARD_KEY_2              3
#define COM_IO_KEYBOARD_KEY_3              4
#define COM_IO_KEYBOARD_KEY_4              5
#define COM_IO_KEYBOARD_KEY_5              6
#define COM_IO_KEYBOARD_KEY_6              7
#define COM_IO_KEYBOARD_KEY_7              8
#define COM_IO_KEYBOARD_KEY_8              9
#define COM_IO_KEYBOARD_KEY_9              10
#define COM_IO_KEYBOARD_KEY_0              11
#define COM_IO_KEYBOARD_KEY_MINUS          12
#define COM_IO_KEYBOARD_KEY_EQUAL          13
#define COM_IO_KEYBOARD_KEY_BACKSPACE      14
#define COM_IO_KEYBOARD_KEY_TAB            15
#define COM_IO_KEYBOARD_KEY_Q              16
#define COM_IO_KEYBOARD_KEY_W              17
#define COM_IO_KEYBOARD_KEY_E              18
#define COM_IO_KEYBOARD_KEY_R              19
#define COM_IO_KEYBOARD_KEY_T              20
#define COM_IO_KEYBOARD_KEY_Y              21
#define COM_IO_KEYBOARD_KEY_U              22
#define COM_IO_KEYBOARD_KEY_I              23
#define COM_IO_KEYBOARD_KEY_O              24
#define COM_IO_KEYBOARD_KEY_P              25
#define COM_IO_KEYBOARD_KEY_LEFTBRACE      26
#define COM_IO_KEYBOARD_KEY_RIGHTBRACE     27
#define COM_IO_KEYBOARD_KEY_ENTER          28
#define COM_IO_KEYBOARD_KEY_LEFTCTRL       29
#define COM_IO_KEYBOARD_KEY_A              30
#define COM_IO_KEYBOARD_KEY_S              31
#define COM_IO_KEYBOARD_KEY_D              32
#define COM_IO_KEYBOARD_KEY_F              33
#define COM_IO_KEYBOARD_KEY_G              34
#define COM_IO_KEYBOARD_KEY_H              35
#define COM_IO_KEYBOARD_KEY_J              36
#define COM_IO_KEYBOARD_KEY_K              37
#define COM_IO_KEYBOARD_KEY_L              38
#define COM_IO_KEYBOARD_KEY_SEMICOLON      39
#define COM_IO_KEYBOARD_KEY_APOSTROPHE     40
#define COM_IO_KEYBOARD_KEY_GRAVE          41
#define COM_IO_KEYBOARD_KEY_LEFTSHIFT      42
#define COM_IO_KEYBOARD_KEY_BACKSLASH      43
#define COM_IO_KEYBOARD_KEY_Z              44
#define COM_IO_KEYBOARD_KEY_X              45
#define COM_IO_KEYBOARD_KEY_C              46
#define COM_IO_KEYBOARD_KEY_V              47
#define COM_IO_KEYBOARD_KEY_B              48
#define COM_IO_KEYBOARD_KEY_N              49
#define COM_IO_KEYBOARD_KEY_M              50
#define COM_IO_KEYBOARD_KEY_COMMA          51
#define COM_IO_KEYBOARD_KEY_DOT            52
#define COM_IO_KEYBOARD_KEY_SLASH          53
#define COM_IO_KEYBOARD_KEY_RIGHTSHIFT     54
#define COM_IO_KEYBOARD_KEY_KEYPADASTERISK 55
#define COM_IO_KEYBOARD_KEY_LEFTALT        56
#define COM_IO_KEYBOARD_KEY_SPACE          57
#define COM_IO_KEYBOARD_KEY_CAPSLOCK       58
#define COM_IO_KEYBOARD_KEY_F1             59
#define COM_IO_KEYBOARD_KEY_F2             60
#define COM_IO_KEYBOARD_KEY_F3             61
#define COM_IO_KEYBOARD_KEY_F4             62
#define COM_IO_KEYBOARD_KEY_F5             63
#define COM_IO_KEYBOARD_KEY_F6             64
#define COM_IO_KEYBOARD_KEY_F7             65
#define COM_IO_KEYBOARD_KEY_F8             66
#define COM_IO_KEYBOARD_KEY_F9             67
#define COM_IO_KEYBOARD_KEY_F10            68
#define COM_IO_KEYBOARD_KEY_NUMLOCK        69
#define COM_IO_KEYBOARD_KEY_SCROLLLOCK     70
#define COM_IO_KEYBOARD_KEY_KEYPAD7        71
#define COM_IO_KEYBOARD_KEY_KEYPAD8        72
#define COM_IO_KEYBOARD_KEY_KEYPAD9        73
#define COM_IO_KEYBOARD_KEY_KEYPADMINUS    74
#define COM_IO_KEYBOARD_KEY_KEYPAD4        75
#define COM_IO_KEYBOARD_KEY_KEYPAD5        76
#define COM_IO_KEYBOARD_KEY_KEYPAD6        77
#define COM_IO_KEYBOARD_KEY_KEYPADPLUS     78
#define COM_IO_KEYBOARD_KEY_KEYPAD1        79
#define COM_IO_KEYBOARD_KEY_KEYPAD2        80
#define COM_IO_KEYBOARD_KEY_KEYPAD3        81
#define COM_IO_KEYBOARD_KEY_KEYPAD0        82
#define COM_IO_KEYBOARD_KEY_KEYPADDOT      83
#define COM_IO_KEYBOARD_KEY_F11            87
#define COM_IO_KEYBOARD_KEY_F12            88
#define COM_IO_KEYBOARD_KEY_KEYPADENTER    96
#define COM_IO_KEYBOARD_KEY_RIGHTCTRL      97
#define COM_IO_KEYBOARD_KEY_KEYPADSLASH    98
#define COM_IO_KEYBOARD_KEY_SYSREQ         99
#define COM_IO_KEYBOARD_KEY_RIGHTALT       100
#define COM_IO_KEYBOARD_KEY_LINEFEED       101
#define COM_IO_KEYBOARD_KEY_HOME           102
#define COM_IO_KEYBOARD_KEY_UP             103
#define COM_IO_KEYBOARD_KEY_PAGEUP         104
#define COM_IO_KEYBOARD_KEY_LEFT           105
#define COM_IO_KEYBOARD_KEY_RIGHT          106
#define COM_IO_KEYBOARD_KEY_END            107
#define COM_IO_KEYBOARD_KEY_DOWN           108
#define COM_IO_KEYBOARD_KEY_PAGEDOWN       109
#define COM_IO_KEYBOARD_KEY_INSERT         110
#define COM_IO_KEYBOARD_KEY_DELETE         111

#define COM_IO_KEYBOARD_KEYSTATE_PRESSED  1
#define COM_IO_KEYBOARD_KEYSTATE_RELEASED 2

typedef struct com_kbd_packet {
    struct com_keyboard *keyboard;

    // Set by the keyboard driverj
    int keycode;
    int keystate;

    // Set by the layout driver
    // (used for kernel TTYs)
    char c;
} com_kbd_packet_t;

// Used by the layout driver to tell the input driver what to do with the
// returned packet
typedef enum com_kbd_action {
    E_COM_KBD_ACTION_SEND,
    E_COM_KBD_ACTION_DISCARD // for dead keys
} com_kbd_action_t;

typedef com_kbd_action_t (*com_intf_kbd_layout_t)(com_kbd_packet_t *pkt);

typedef struct com_keyboard {
    com_intf_kbd_layout_t layout;
    com_vnode_t          *vnode;
    kringbuffer_t         rb;
    struct com_poll_head  pollhead;

    // used by the layout driver to when to use uppercase
    struct {
        bool shift_held;
        bool caps_lock;
    } state;
} com_keyboard_t;

int             com_io_keyboard_send_packet(com_keyboard_t   *keyboard,
                                            com_kbd_packet_t *pkt);
com_keyboard_t *com_io_keyboard_new(com_intf_kbd_layout_t layout);

// LAYOUTS

com_kbd_action_t com_io_keyboard_layout_en_us(com_kbd_packet_t *pkt);
