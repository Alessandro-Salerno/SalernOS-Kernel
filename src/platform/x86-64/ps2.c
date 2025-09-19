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
#include <kernel/com/io/mouse.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/ioapic.h>
#include <kernel/platform/x86-64/ps2.h>

#define PS2_KEYBOARD_EXTENDED_SCANCODE 0xE0
#define PS2_KEYBOARD_KEY_RELEASED      0x80

#define PS2_MOUSE    0
#define PS2_MOUSE_Z  3
#define PS2_MOUSE_5B 4

#define PS2_MOUSECTL_SAMPLERATE 0xF3
#define PS2_MOUSECTL_PULSE4     0xF4
#define PS2_MOUSECTL_PULSE6     0xF6
#define PS2_MOUSECTL_PULSE10    0xFA

#define PS2_MOUSEPKT_GOOD 8
#define PS2_MOUSEPKT_ENOUGH(mouse, cycle) \
    (((mouse)->has_wheel && 4 == cycle) || (!(mouse)->has_wheel && 3 == cycle))
#define PS2_MOUSEPKT_TO_PROTOCOL(data)                 \
    (1 & data[0] ? COM_IO_MOUSE_LEFTBUTTON : 0) |      \
        (2 & data[0] ? COM_IO_MOUSE_RIGHTBUTTON : 0) | \
        (4 & data[0] ? COM_IO_MOUSE_MIDBUTTON : 0) |   \
        (0x10 & data[3] ? COM_IO_MOUSE_BUTTON4 : 0) |  \
        (0x20 & data[3] ? COM_IO_MOUSE_BUTTON5 : 0)

#define PS2_PORT_DATA          0x60
#define PS2_PORT_CNTL          0x64
#define PS2_PORT_KEYBOARD_CNTL 0x61

#define PS2_CNTL_DISABLE_KEYBAORD 0xAD
#define PS2_CNTL_DISABLE_MOUSE    0xA7
#define PS2_CNTL_ENABLE_KEYBAORD  0xAE
#define PS2_CNTL_ENABLE_MOUSE     0xA8
#define PS2_CNTL_READ_CFG         0x20
#define PS2_CNTL_WRITE_CFG        0x60
#define PS2_CNTL_WRITE_MOUSE      0xD4

#define PS2_STATUS_OUTDATA 1
#define PS2_STATUS_INDATA  2

#define PS2_CFG_IRQ0_ENABLE        (1 << 0)
#define PS2_CFG_IRQ1_ENABLE        (1 << 1)
#define PS2_CFG_MOUSE_EXISTS       (1 << 5)
#define PS2_CFG_TRANSLATION_ENABLE (1 << 6)

struct ps2_mouse {
    com_mouse_t *mouse;
    bool         has_wheel;
};

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

static com_keyboard_t  *Ps2Keyboard = NULL;
static struct ps2_mouse Ps2Mouse    = {0};

// UTILITY CODE

static void ps2_write(uint16_t port, uint8_t val) {
    while (2 & X86_64_IO_INB(0x64)) {
        ARCH_CPU_PAUSE();
    }

    X86_64_IO_OUTB(port, val);
}

static uint16_t ps2_read(uint16_t port) {
    while (X86_64_IO_INB(0x64) & 2) asm("pause");
    return X86_64_IO_INB(port);
}

static void ps2_mouse_wait(uint8_t bit) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (PS2_STATUS_OUTDATA == bit &&
            1 == (PS2_STATUS_OUTDATA & X86_64_IO_INB(PS2_PORT_CNTL))) {
            return;
        }
        if (PS2_STATUS_INDATA == bit &&
            0 == (PS2_STATUS_INDATA & X86_64_IO_INB(PS2_PORT_CNTL))) {
            return;
        }
    }
}

static void ps2_mouse_write(uint8_t val) {
    ps2_mouse_wait(PS2_STATUS_INDATA);
    X86_64_IO_OUTB(PS2_PORT_CNTL, PS2_CNTL_WRITE_MOUSE);
    ps2_mouse_wait(PS2_STATUS_INDATA);
    X86_64_IO_OUTB(PS2_PORT_DATA, val);
}

static uint8_t ps2_mouse_read(void) {
    ps2_mouse_wait(PS2_STATUS_OUTDATA);
    return X86_64_IO_INB(PS2_PORT_DATA);
}

// KEYBOARD HANDLERS

static void ps2_keyboard_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
    (void)ctx;

    static bool extended = false;
    uint8_t     scancode = X86_64_IO_INB(PS2_PORT_DATA);

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

/*static void ps2_keyboard_eoi(com_isr_t *isr) {
    (void)isr;
    X86_64_IO_OUTB(0x20, 0x20);
}*/

// MOUSE HANDLERS

// CREDIT: Mathewnd/Astral
static void ps2_mouse_isr(com_isr_t *isr, arch_context_t *ctx) {
    (void)isr;
    (void)ctx;

    static uint8_t mouse_data[5] = {0};
    static size_t  cycle         = 0;

    uint8_t curr_byte = X86_64_IO_INB(PS2_PORT_DATA);
    mouse_data[cycle] = curr_byte;
    if (!(PS2_MOUSEPKT_GOOD & mouse_data[0])) {
        cycle = 0;
        return;
    }

    cycle++;
    if (!PS2_MOUSEPKT_ENOUGH(&Ps2Mouse, cycle)) {
        return;
    }

    com_mouse_packet_t pkt = {0};
    pkt.mouse              = Ps2Mouse.mouse;
    pkt.buttons            = PS2_MOUSEPKT_TO_PROTOCOL(mouse_data);
    pkt.dx                 = mouse_data[1] - (0x10 & mouse_data[0] ? 0x100 : 0);
    pkt.dy                 = mouse_data[2] - (0x20 & mouse_data[0] ? 0x100 : 0);
    pkt.dy                 = -pkt.dy;
    pkt.dz = (0x07 & mouse_data[3]) * (0x08 & mouse_data[3] ? -1 : 1);

    cycle = 0;
    com_io_console_mouse_in(&pkt);
}

/*static void ps2_mouse_eoi(com_isr_t *isr) {
    (void)isr;
    X86_64_IO_OUTB(0xA0, 0x20);
    X86_64_IO_OUTB(0x20, 0x20);
}*/

// SETUP CODE

void x86_64_ps2_init(void) {
    KLOG("initializing ps/2 controller");

    ps2_write(PS2_PORT_CNTL, PS2_CNTL_DISABLE_KEYBAORD);
    ps2_write(PS2_PORT_CNTL, PS2_CNTL_DISABLE_MOUSE);

    // FLush input buffer
    while (PS2_STATUS_OUTDATA & X86_64_IO_INB(PS2_PORT_CNTL)) {
        X86_64_IO_INB(PS2_PORT_DATA);
    }

    ps2_write(PS2_PORT_CNTL, PS2_CNTL_READ_CFG);
    uint8_t cfg = ps2_read(PS2_PORT_DATA);
    cfg |= PS2_CFG_IRQ0_ENABLE | PS2_CFG_TRANSLATION_ENABLE;

    // Only enable mouse if it exists
    if (PS2_CFG_MOUSE_EXISTS & cfg) {
        cfg |= PS2_CFG_IRQ1_ENABLE;
    }

    ps2_write(PS2_PORT_CNTL, PS2_CNTL_WRITE_CFG);
    ps2_write(PS2_PORT_DATA, cfg);

    // Re-enable devices that were disabled at the beginning (if available)
    X86_64_IO_OUTB(PS2_PORT_CNTL, PS2_CNTL_ENABLE_KEYBAORD);
    if (PS2_CFG_MOUSE_EXISTS & cfg) {
        X86_64_IO_OUTB(PS2_PORT_CNTL, PS2_CNTL_ENABLE_MOUSE);
    }
}

void x86_64_ps2_keyboard_init(void) {
    KLOG("initializing ps/2 keyboard");
    Ps2Keyboard        = com_io_keyboard_new(NULL);
    com_isr_t *kbd_isr = com_sys_interrupt_allocate(ps2_keyboard_isr,
                                                    x86_64_lapic_eoi);
    x86_64_ioapic_set_irq(X86_64_IOAPIC_IRQ_KEYBOARD,
                          kbd_isr->vec,
                          ARCH_CPU_GET_HARDWARE_ID(),
                          false);
}

// CREDIT: vloxei64/ke
void x86_64_ps2_mouse_init(void) {
    KLOG("initializing ps/2 mouse");

    // Enable mouse device
    ps2_mouse_wait(PS2_STATUS_INDATA);
    X86_64_IO_OUTB(PS2_PORT_CNTL, PS2_CNTL_ENABLE_MOUSE);
    ps2_mouse_wait(PS2_STATUS_INDATA);

    // Read current configuration
    X86_64_IO_OUTB(PS2_PORT_CNTL, PS2_CNTL_READ_CFG);
    ps2_mouse_wait(PS2_STATUS_OUTDATA);
    uint8_t cfg = X86_64_IO_INB(PS2_PORT_DATA);

    // Enable mouse IRQ and write new configuration
    cfg |= PS2_CFG_IRQ1_ENABLE;
    ps2_mouse_wait(PS2_STATUS_INDATA);
    X86_64_IO_OUTB(PS2_PORT_CNTL, PS2_CNTL_WRITE_CFG);
    ps2_mouse_wait(PS2_STATUS_INDATA);
    X86_64_IO_OUTB(PS2_PORT_DATA, cfg);

    // No idea what is going on here
    ps2_mouse_write(PS2_MOUSECTL_PULSE6);
    ps2_mouse_read();
    ps2_mouse_write(PS2_MOUSECTL_PULSE4);
    ps2_mouse_read();

    X86_64_IO_OUTB(PS2_PORT_CNTL, PS2_CNTL_WRITE_MOUSE);
    X86_64_IO_OUTB(PS2_PORT_DATA, PS2_MOUSECTL_SAMPLERATE);
    uint8_t sample_rate = X86_64_IO_INB(PS2_PORT_DATA);
    if (PS2_MOUSECTL_PULSE10 != sample_rate) {
        KDEBUG("received unexpcted value for PS2_MOUSECTL_SAMPLERATE: expected "
               "%x, got %x",
               PS2_MOUSECTL_PULSE10,
               sample_rate);
    }

    X86_64_IO_OUTB(PS2_PORT_CNTL, PS2_CNTL_WRITE_MOUSE);
    X86_64_IO_OUTB(PS2_PORT_DATA, 10);
    sample_rate = X86_64_IO_INB(PS2_PORT_DATA);
    if (PS2_MOUSECTL_PULSE10 != sample_rate) {
        KDEBUG("received unexpcted value for PS2_MOUSECTL_SAMPLERATE: expected "
               "%x, got %x",
               PS2_MOUSECTL_PULSE10,
               sample_rate);
    }

    Ps2Mouse.mouse       = com_io_mouse_new();
    Ps2Mouse.has_wheel   = false;
    com_isr_t *mouse_isr = com_sys_interrupt_allocate(ps2_mouse_isr,
                                                      x86_64_lapic_eoi);
    x86_64_ioapic_set_irq(X86_64_IOAPIC_IRQ_MOUSE,
                          mouse_isr->vec,
                          ARCH_CPU_GET_HARDWARE_ID(),
                          false);
}
