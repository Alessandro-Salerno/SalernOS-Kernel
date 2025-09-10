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
#include <kernel/com/io/keyboard.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/ps2.h>

#define PS2_KEYBOARD_EXTENDED_SCANCODE 0xE0
#define PS2_KEYBOARD_KEY_RELEASED      0x80

// TAKEN: Mathewnd/Astral
static const char G_PS2_KEYBOARD_SCANCODES[128] = {
    COM_IO_KEYBOARD_KEY_RESERVED,
    COM_IO_KEYBOARD_KEY_ESCAPE,
    COM_IO_KEYBOARD_KEY_1,
    COM_IO_KEYBOARD_KEY_2,
    COM_IO_KEYBOARD_KEY_3,
    COM_IO_KEYBOARD_KEY_4,
    COM_IO_KEYBOARD_KEY_5,
    COM_IO_KEYBOARD_KEY_6,
    COM_IO_KEYBOARD_KEY_7,
    COM_IO_KEYBOARD_KEY_8,
    COM_IO_KEYBOARD_KEY_9,
    COM_IO_KEYBOARD_KEY_0,
    COM_IO_KEYBOARD_KEY_MINUS,
    COM_IO_KEYBOARD_KEY_EQUAL,
    COM_IO_KEYBOARD_KEY_BACKSPACE,
    COM_IO_KEYBOARD_KEY_TAB,
    COM_IO_KEYBOARD_KEY_Q,
    COM_IO_KEYBOARD_KEY_W,
    COM_IO_KEYBOARD_KEY_E,
    COM_IO_KEYBOARD_KEY_R,
    COM_IO_KEYBOARD_KEY_T,
    COM_IO_KEYBOARD_KEY_Y,
    COM_IO_KEYBOARD_KEY_U,
    COM_IO_KEYBOARD_KEY_I,
    COM_IO_KEYBOARD_KEY_O,
    COM_IO_KEYBOARD_KEY_P,
    COM_IO_KEYBOARD_KEY_LEFTBRACE,
    COM_IO_KEYBOARD_KEY_RIGHTBRACE,
    COM_IO_KEYBOARD_KEY_ENTER,
    COM_IO_KEYBOARD_KEY_LEFTCTRL,
    COM_IO_KEYBOARD_KEY_A,
    COM_IO_KEYBOARD_KEY_S,
    COM_IO_KEYBOARD_KEY_D,
    COM_IO_KEYBOARD_KEY_F,
    COM_IO_KEYBOARD_KEY_G,
    COM_IO_KEYBOARD_KEY_H,
    COM_IO_KEYBOARD_KEY_J,
    COM_IO_KEYBOARD_KEY_K,
    COM_IO_KEYBOARD_KEY_L,
    COM_IO_KEYBOARD_KEY_SEMICOLON,
    COM_IO_KEYBOARD_KEY_APOSTROPHE,
    COM_IO_KEYBOARD_KEY_GRAVE,
    COM_IO_KEYBOARD_KEY_LEFTSHIFT,
    COM_IO_KEYBOARD_KEY_BACKSLASH,
    COM_IO_KEYBOARD_KEY_Z,
    COM_IO_KEYBOARD_KEY_X,
    COM_IO_KEYBOARD_KEY_C,
    COM_IO_KEYBOARD_KEY_V,
    COM_IO_KEYBOARD_KEY_B,
    COM_IO_KEYBOARD_KEY_N,
    COM_IO_KEYBOARD_KEY_M,
    COM_IO_KEYBOARD_KEY_COMMA,
    COM_IO_KEYBOARD_KEY_DOT,
    COM_IO_KEYBOARD_KEY_SLASH,
    COM_IO_KEYBOARD_KEY_RIGHTSHIFT,
    COM_IO_KEYBOARD_KEY_KEYPADASTERISK,
    COM_IO_KEYBOARD_KEY_LEFTALT,
    COM_IO_KEYBOARD_KEY_SPACE,
    COM_IO_KEYBOARD_KEY_CAPSLOCK,
    COM_IO_KEYBOARD_KEY_F1,
    COM_IO_KEYBOARD_KEY_F2,
    COM_IO_KEYBOARD_KEY_F3,
    COM_IO_KEYBOARD_KEY_F4,
    COM_IO_KEYBOARD_KEY_F5,
    COM_IO_KEYBOARD_KEY_F6,
    COM_IO_KEYBOARD_KEY_F7,
    COM_IO_KEYBOARD_KEY_F8,
    COM_IO_KEYBOARD_KEY_F9,
    COM_IO_KEYBOARD_KEY_F10,
    COM_IO_KEYBOARD_KEY_NUMLOCK,
    COM_IO_KEYBOARD_KEY_SCROLLLOCK,
    COM_IO_KEYBOARD_KEY_KEYPAD7,
    COM_IO_KEYBOARD_KEY_KEYPAD8,
    COM_IO_KEYBOARD_KEY_KEYPAD9,
    COM_IO_KEYBOARD_KEY_KEYPADMINUS,
    COM_IO_KEYBOARD_KEY_KEYPAD4,
    COM_IO_KEYBOARD_KEY_KEYPAD5,
    COM_IO_KEYBOARD_KEY_KEYPAD6,
    COM_IO_KEYBOARD_KEY_KEYPADPLUS,
    COM_IO_KEYBOARD_KEY_KEYPAD1,
    COM_IO_KEYBOARD_KEY_KEYPAD2,
    COM_IO_KEYBOARD_KEY_KEYPAD3,
    COM_IO_KEYBOARD_KEY_KEYPAD0,
    COM_IO_KEYBOARD_KEY_KEYPADDOT,
    0,
    0,
    0,
    COM_IO_KEYBOARD_KEY_F11,
    COM_IO_KEYBOARD_KEY_F12};

static const char G_PS2_KEYBOARD_EXT_SCANCODES[128] = {
    [0x1C] = COM_IO_KEYBOARD_KEY_KEYPADENTER,
    [0x1D] = COM_IO_KEYBOARD_KEY_RIGHTCTRL,
    [0x35] = COM_IO_KEYBOARD_KEY_KEYPADSLASH, // k/
    [0x38] = COM_IO_KEYBOARD_KEY_RIGHTALT,    // altgr
    [0x47] = COM_IO_KEYBOARD_KEY_HOME,        // home
    [0x48] = COM_IO_KEYBOARD_KEY_UP,          // up
    [0x49] = COM_IO_KEYBOARD_KEY_PAGEUP,      // page up
    [0x4B] = COM_IO_KEYBOARD_KEY_LEFT,        // left
    [0x4D] = COM_IO_KEYBOARD_KEY_RIGHT,       // right
    [0x4F] = COM_IO_KEYBOARD_KEY_END,         // end
    [0x50] = COM_IO_KEYBOARD_KEY_DOWN,        // down
    [0x51] = COM_IO_KEYBOARD_KEY_PAGEDOWN,    // page down
    [0x52] = COM_IO_KEYBOARD_KEY_INSERT,      // insert
    [0x53] = COM_IO_KEYBOARD_KEY_DELETE       // delete
};

static com_keyboard_t *Ps2Keyboard = NULL;

static void ps2_keyboard_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
    (void)ctx;

    static bool extended = false;
    uint8_t     scancode = X86_64_IO_INB(0x60);

    if (PS2_KEYBOARD_EXTENDED_SCANCODE == scancode) {
        extended = true;
        return;
    }

    com_kbd_packet_t pkt = {0};
    pkt.keyboard         = Ps2Keyboard;
    pkt.keystate         = COM_IO_KEYBOARD_KEYSTATE_PRESSED;

    if (PS2_KEYBOARD_KEY_RELEASED & scancode) {
        pkt.keystate = COM_IO_KEYBOARD_KEYSTATE_RELEASED;
        scancode &= ~PS2_KEYBOARD_KEY_RELEASED;
    }

    const char *keycode_lookup = G_PS2_KEYBOARD_SCANCODES;
    if (extended) {
        keycode_lookup = G_PS2_KEYBOARD_EXT_SCANCODES;
        extended       = false;
    }

    pkt.keycode = keycode_lookup[scancode];

    if (0 == pkt.keycode) {
        return;
    }

    com_kbd_action_t action = pkt.keyboard->layout(&pkt);
    if (E_COM_KBD_ACTION_SEND == action) {
        if (COM_IO_KEYBOARD_KEY_CAPSLOCK == pkt.keycode) {
            pkt.keyboard->state.caps_lock ^= COM_IO_KEYBOARD_KEYSTATE_PRESSED ==
                                             pkt.keystate;
        }

        if (COM_IO_KEYBOARD_KEY_LEFTSHIFT == pkt.keycode ||
            COM_IO_KEYBOARD_KEY_RIGHTSHIFT == pkt.keycode) {
            pkt.keyboard->state.shift_held = COM_IO_KEYBOARD_KEYSTATE_PRESSED ==
                                             pkt.keystate;
        }

        com_io_console_kbd_in(&pkt);
    }
}

static void ps2_keyboard_eoi(com_isr_t *isr) {
    (void)isr;
    X86_64_IO_OUTB(0x20, 0x20);
}

void x86_64_ps2_keyboard_init(void) {
    KLOG("initializing ps/2 keyboard");
    Ps2Keyboard = com_io_keyboard_new(NULL);
    com_sys_interrupt_register(0x21, ps2_keyboard_isr, ps2_keyboard_eoi);
}
