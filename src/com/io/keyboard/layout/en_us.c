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

#include <kernel/com/io/keyboard.h>

// TAKEN: Mathewnd/Astral
static const char G_LAYOUT_EN_US_MAP[] = {
    0,    '\033', '1', '2',  '3', '4', '5', '6', '7', '8', '9', '0', '-',  '=',
    '\b', '\t',   'q', 'w',  'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',  ']',
    '\r', 0,      'a', 's',  'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\',   'z', 'x',  'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,    '*',
    0,    ' ',    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',    '8', '9',  '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.',
    0,    0,      '<', 0,    0,   0,   0,   0,   0,   0,   0,   0,   '\r', 0,
    '/',  0,      0,   '\r', 0,   0,   0,   0,   0,   0,   0,   0,   0,    0};

static const char G_LAYOUT_EN_US_MAP_SHIFTED[] = {
    0,    '\033', '!', '@',  '#', '$', '%', '^', '&', '*', '(', ')', '_',  '+',
    '\b', '\t',   'Q', 'W',  'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{',  '}',
    '\r', 0,      'A', 'S',  'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"',  '~',
    0,    '|',    'Z', 'X',  'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,    '*',
    0,    ' ',    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,    0,
    0,    '7',    '8', '9',  '-', '4', '5', '6', '+', '1', '2', '3', '0',  '.',
    0,    0,      '<', 0,    0,   0,   0,   0,   0,   0,   0,   0,   '\r', 0,
    '/',  0,      0,   '\r', 0,   0,   0,   0,   0,   0,   0,   0,   0,    0};

com_kbd_action_t com_io_keyboard_layout_en_us(com_kbd_packet_t *pkt) {
    com_keyboard_t *keyboard = pkt->keyboard;

    if (pkt->keycode >= 0x80) {
        return E_COM_KBD_ACTION_DISCARD;
    }

    if (keyboard->state.shift_held ^ keyboard->state.caps_lock) {
        pkt->c = G_LAYOUT_EN_US_MAP_SHIFTED[pkt->keycode];
    } else {
        pkt->c = G_LAYOUT_EN_US_MAP[pkt->keycode];
    }

    return E_COM_KBD_ACTION_SEND;
}
