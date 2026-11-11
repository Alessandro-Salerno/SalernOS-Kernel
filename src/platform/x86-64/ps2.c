#include <kernel/com/io/console.h>
#include <kernel/com/io/keyboard.h>
#include <kernel/com/io/mouse.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/util.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/ioapic.h>
#include <kernel/platform/x86-64/ps2.h>
#include <kernel/platform/x86-64/tsc.h>

#define PS2_KEYBOARD_EXTENDED_SCANCODE 0xE0
#define PS2_KEYBOARD_KEY_RELEASED      0x80

#define PS2_MOUSE    0
#define PS2_MOUSE_Z  3
#define PS2_MOUSE_5B 4

#define PS2_MOUSECTL_SAMPLERATE 0xF3
#define PS2_MOUSECTL_PULSE4     0xF4
#define PS2_MOUSECTL_PULSE6     0xF6
#define PS2_MOUSECTL_PULSE10    0xFA

#define PS2_MOUSEPKT_GOOD       8
#define PS2_MOUSEPKT_XOVERFLOW  0x40
#define PS2_MOUSEPKT_YOVERFLOW  0x80
#define PS2_MOUSEPKT_TIMEOUT_NS 100000000
#define PS2_MOUSEPKT_SYNCED(byte)  \
    ((byte) & PS2_MOUSEPKT_GOOD && \
     ((byte) & (PS2_MOUSEPKT_XOVERFLOW | PS2_MOUSEPKT_YOVERFLOW)) == 0)
#define PS2_MOUSEPKT_ENOUGH(mouse, cycle) \
    (((mouse)->has_wheel && cycle == 4) || (!(mouse)->has_wheel && cycle == 3))
#define PS2_MOUSEPKT_TO_PROTOCOL(data)          \
    (data[0] & 1 ? MOUSE_LEFTBUTTON : 0) |      \
        (data[0] & 2 ? MOUSE_RIGHTBUTTON : 0) | \
        (data[0] & 4 ? MOUSE_MIDBUTTON : 0) |   \
        (data[3] & 0x10 ? MOUSE_BUTTON4 : 0) |  \
        (data[3] & 0x20 ? MOUSE_BUTTON5 : 0)

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
#define PS2_STATUS_AUXDATA 0x20

#define PS2_CFG_IRQ0_ENABLE        (1 << 0)
#define PS2_CFG_IRQ1_ENABLE        (1 << 1)
#define PS2_CFG_MOUSE_EXISTS       (1 << 5)
#define PS2_CFG_TRANSLATION_ENABLE (1 << 6)

struct PS2Keyboard {
    Keyboard *keyboard;
    bool extended;
};

struct PS2Mouse {
    Mouse *mouse;
    bool has_wheel;
    u8 data[5];
    usize cycle;
    u64 last_time;
};

// TAKEN: Mathewnd/Astral
static const char G_PS2_KEYBOARD_SCANCODES[128] = {KEYBOARD_KEY_RESERVED,
                                                   KEYBOARD_KEY_ESCAPE,
                                                   KEYBOARD_KEY_1,
                                                   KEYBOARD_KEY_2,
                                                   KEYBOARD_KEY_3,
                                                   KEYBOARD_KEY_4,
                                                   KEYBOARD_KEY_5,
                                                   KEYBOARD_KEY_6,
                                                   KEYBOARD_KEY_7,
                                                   KEYBOARD_KEY_8,
                                                   KEYBOARD_KEY_9,
                                                   KEYBOARD_KEY_0,
                                                   KEYBOARD_KEY_MINUS,
                                                   KEYBOARD_KEY_EQUAL,
                                                   KEYBOARD_KEY_BACKSPACE,
                                                   KEYBOARD_KEY_TAB,
                                                   KEYBOARD_KEY_Q,
                                                   KEYBOARD_KEY_W,
                                                   KEYBOARD_KEY_E,
                                                   KEYBOARD_KEY_R,
                                                   KEYBOARD_KEY_T,
                                                   KEYBOARD_KEY_Y,
                                                   KEYBOARD_KEY_U,
                                                   KEYBOARD_KEY_I,
                                                   KEYBOARD_KEY_O,
                                                   KEYBOARD_KEY_P,
                                                   KEYBOARD_KEY_LEFTBRACE,
                                                   KEYBOARD_KEY_RIGHTBRACE,
                                                   KEYBOARD_KEY_ENTER,
                                                   KEYBOARD_KEY_LEFTCTRL,
                                                   KEYBOARD_KEY_A,
                                                   KEYBOARD_KEY_S,
                                                   KEYBOARD_KEY_D,
                                                   KEYBOARD_KEY_F,
                                                   KEYBOARD_KEY_G,
                                                   KEYBOARD_KEY_H,
                                                   KEYBOARD_KEY_J,
                                                   KEYBOARD_KEY_K,
                                                   KEYBOARD_KEY_L,
                                                   KEYBOARD_KEY_SEMICOLON,
                                                   KEYBOARD_KEY_APOSTROPHE,
                                                   KEYBOARD_KEY_GRAVE,
                                                   KEYBOARD_KEY_LEFTSHIFT,
                                                   KEYBOARD_KEY_BACKSLASH,
                                                   KEYBOARD_KEY_Z,
                                                   KEYBOARD_KEY_X,
                                                   KEYBOARD_KEY_C,
                                                   KEYBOARD_KEY_V,
                                                   KEYBOARD_KEY_B,
                                                   KEYBOARD_KEY_N,
                                                   KEYBOARD_KEY_M,
                                                   KEYBOARD_KEY_COMMA,
                                                   KEYBOARD_KEY_DOT,
                                                   KEYBOARD_KEY_SLASH,
                                                   KEYBOARD_KEY_RIGHTSHIFT,
                                                   KEYBOARD_KEY_KEYPADASTERISK,
                                                   KEYBOARD_KEY_LEFTALT,
                                                   KEYBOARD_KEY_SPACE,
                                                   KEYBOARD_KEY_CAPSLOCK,
                                                   KEYBOARD_KEY_F1,
                                                   KEYBOARD_KEY_F2,
                                                   KEYBOARD_KEY_F3,
                                                   KEYBOARD_KEY_F4,
                                                   KEYBOARD_KEY_F5,
                                                   KEYBOARD_KEY_F6,
                                                   KEYBOARD_KEY_F7,
                                                   KEYBOARD_KEY_F8,
                                                   KEYBOARD_KEY_F9,
                                                   KEYBOARD_KEY_F10,
                                                   KEYBOARD_KEY_NUMLOCK,
                                                   KEYBOARD_KEY_SCROLLLOCK,
                                                   KEYBOARD_KEY_KEYPAD7,
                                                   KEYBOARD_KEY_KEYPAD8,
                                                   KEYBOARD_KEY_KEYPAD9,
                                                   KEYBOARD_KEY_KEYPADMINUS,
                                                   KEYBOARD_KEY_KEYPAD4,
                                                   KEYBOARD_KEY_KEYPAD5,
                                                   KEYBOARD_KEY_KEYPAD6,
                                                   KEYBOARD_KEY_KEYPADPLUS,
                                                   KEYBOARD_KEY_KEYPAD1,
                                                   KEYBOARD_KEY_KEYPAD2,
                                                   KEYBOARD_KEY_KEYPAD3,
                                                   KEYBOARD_KEY_KEYPAD0,
                                                   KEYBOARD_KEY_KEYPADDOT,
                                                   0,
                                                   0,
                                                   0,
                                                   KEYBOARD_KEY_F11,
                                                   KEYBOARD_KEY_F12};

static const char G_PS2_KEYBOARD_EXT_SCANCODES[128] = {
    [0x1C] = KEYBOARD_KEY_KEYPADENTER, [0x1D] = KEYBOARD_KEY_RIGHTCTRL,
    [0x35] = KEYBOARD_KEY_KEYPADSLASH, // k/
    [0x38] = KEYBOARD_KEY_RIGHTALT,    // altgr
    [0x47] = KEYBOARD_KEY_HOME,        // home
    [0x48] = KEYBOARD_KEY_UP,          // up
    [0x49] = KEYBOARD_KEY_PAGEUP,      // page up
    [0x4B] = KEYBOARD_KEY_LEFT,        // left
    [0x4D] = KEYBOARD_KEY_RIGHT,       // right
    [0x4F] = KEYBOARD_KEY_END,         // end
    [0x50] = KEYBOARD_KEY_DOWN,        // down
    [0x51] = KEYBOARD_KEY_PAGEDOWN,    // page down
    [0x52] = KEYBOARD_KEY_INSERT,      // insert
    [0x53] = KEYBOARD_KEY_DELETE       // delete
};

static struct PS2Keyboard g_ps2_keyboard = {0};
static struct PS2Mouse g_ps2_mouse = {0};

// UTILITY CODE

static void ps2_write(u16 port, u8 val) {
    while (io_inb(0x64) & 2) cpu_pause();

    io_outb(port, val);
}

static u16 ps2_read(u16 port) {
    while (io_inb(0x64) & 2) asm("pause");
    return io_inb(port);
}

static void ps2_mouse_wait(u8 bit) {
    for (u32 i = 0; i < 100000; i++) {
        if (bit == PS2_STATUS_OUTDATA &&
            (io_inb(PS2_PORT_CNTL) & PS2_STATUS_OUTDATA) == 1) {
            return;
        }
        if (bit == PS2_STATUS_INDATA &&
            (io_inb(PS2_PORT_CNTL) & PS2_STATUS_INDATA) == 0) {
            return;
        }
    }
}

static void ps2_mouse_write(u8 val) {
    ps2_mouse_wait(PS2_STATUS_INDATA);
    io_outb(PS2_PORT_CNTL, PS2_CNTL_WRITE_MOUSE);
    ps2_mouse_wait(PS2_STATUS_INDATA);
    io_outb(PS2_PORT_DATA, val);
}

static u8 ps2_mouse_read(void) {
    ps2_mouse_wait(PS2_STATUS_OUTDATA);
    return io_inb(PS2_PORT_DATA);
}

// KEYBOARD HANDLERS

static void ps2_keyboard_isr(InterruptHandler *isr, Context *ctx) {
    (void)isr;
    (void)ctx;

    u8 scancode = io_inb(PS2_PORT_DATA);

    if (scancode == PS2_KEYBOARD_EXTENDED_SCANCODE) {
        g_ps2_keyboard.extended = true;
        return;
    }

    KbdPacket pkt = {0};
    pkt.keyboard = g_ps2_keyboard.keyboard;
    pkt.keystate = KEYBOARD_KEYSTATE_PRESSED;

    if (scancode & PS2_KEYBOARD_KEY_RELEASED) {
        pkt.keystate = KEYBOARD_KEYSTATE_RELEASED;
        scancode &= ~PS2_KEYBOARD_KEY_RELEASED;
    }

    const char *keycode_lookup = G_PS2_KEYBOARD_SCANCODES;
    if (g_ps2_keyboard.extended) {
        keycode_lookup = G_PS2_KEYBOARD_EXT_SCANCODES;
        g_ps2_keyboard.extended = false;
    }

    pkt.keycode = keycode_lookup[scancode];

    if (pkt.keycode == 0) return;

    console_kbd_in(&pkt);
}

// MOUSE HANDLERS

// CREDIT: Mathewnd/Astral
static void ps2_mouse_isr(InterruptHandler *isr, Context *ctx) {
    (void)isr;
    (void)ctx;

    u8 status = io_inb(PS2_PORT_CNTL);
    if ((status & PS2_STATUS_OUTDATA) == 0 ||
        (status & PS2_STATUS_AUXDATA) == 0) {
        return;
    }

    u8 curr_byte = io_inb(PS2_PORT_DATA);
    u64 now = cpu_get_time();

    if (now - g_ps2_mouse.last_time > PS2_MOUSEPKT_TIMEOUT_NS) {
        g_ps2_mouse.cycle = 0;
    }
    g_ps2_mouse.last_time = now;

    if (g_ps2_mouse.cycle == 0 && !PS2_MOUSEPKT_SYNCED(curr_byte)) return;

    g_ps2_mouse.data[g_ps2_mouse.cycle] = curr_byte;
    g_ps2_mouse.cycle++;
    if (!PS2_MOUSEPKT_ENOUGH(&g_ps2_mouse, g_ps2_mouse.cycle)) return;

    u8 *mouse_data = g_ps2_mouse.data;

    MousePacket pkt = {0};
    pkt.mouse = g_ps2_mouse.mouse;
    pkt.buttons = PS2_MOUSEPKT_TO_PROTOCOL(mouse_data);
    pkt.dx = mouse_data[1] - (mouse_data[0] & 0x10 ? 0x100 : 0);
    pkt.dy = mouse_data[2] - (mouse_data[0] & 0x20 ? 0x100 : 0);
    pkt.dy = -pkt.dy;
    pkt.dz = (mouse_data[3] & 0x07) * (mouse_data[3] & 0x08 ? -1 : 1);

    g_ps2_mouse.cycle = 0;
    console_mouse_in(&pkt);
}

// SETUP CODE

void ps2_init(void) {
    log("initializing ps/2 controller");

    ps2_write(PS2_PORT_CNTL, PS2_CNTL_DISABLE_KEYBAORD);
    ps2_write(PS2_PORT_CNTL, PS2_CNTL_DISABLE_MOUSE);

    // Flush input buffer
    while (io_inb(PS2_PORT_CNTL) & PS2_STATUS_OUTDATA) io_inb(PS2_PORT_DATA);

    ps2_write(PS2_PORT_CNTL, PS2_CNTL_READ_CFG);
    u8 cfg = ps2_read(PS2_PORT_DATA);
    cfg |= PS2_CFG_IRQ0_ENABLE | PS2_CFG_TRANSLATION_ENABLE;

    // Only enable mouse if it exists
    if (cfg & PS2_CFG_MOUSE_EXISTS) cfg |= PS2_CFG_IRQ1_ENABLE;

    ps2_write(PS2_PORT_CNTL, PS2_CNTL_WRITE_CFG);
    ps2_write(PS2_PORT_DATA, cfg);

    // Re-enable devices that were disabled at the beginning (if available)
    io_outb(PS2_PORT_CNTL, PS2_CNTL_ENABLE_KEYBAORD);
    if (cfg & PS2_CFG_MOUSE_EXISTS) {
        io_outb(PS2_PORT_CNTL, PS2_CNTL_ENABLE_MOUSE);
    }
}

void ps2_keyboard_init(void) {
    log("initializing ps/2 keyboard");
    g_ps2_keyboard.keyboard = keyboard_new(NULL);
    InterruptHandler *kbd_isr = interrupt_allocate(ps2_keyboard_isr, lapic_eoi);
    kbd_isr->flags |= INTERRUPT_FLAGS_EOI_AFTER;
    ioapic_set_irq(IOAPIC_IRQ_KEYBOARD, kbd_isr->vec & 0xff,
                   cpu_get_hardware_id(), false);
}

// CREDIT: vloxei64/ke
void ps2_mouse_init(void) {
    log("initializing ps/2 mouse");

    // Enable mouse device
    ps2_mouse_wait(PS2_STATUS_INDATA);
    io_outb(PS2_PORT_CNTL, PS2_CNTL_ENABLE_MOUSE);
    ps2_mouse_wait(PS2_STATUS_INDATA);

    // Read current configuration
    io_outb(PS2_PORT_CNTL, PS2_CNTL_READ_CFG);
    ps2_mouse_wait(PS2_STATUS_OUTDATA);
    u8 cfg = io_inb(PS2_PORT_DATA);

    // Enable mouse IRQ and write new configuration
    cfg |= PS2_CFG_IRQ1_ENABLE;
    ps2_mouse_wait(PS2_STATUS_INDATA);
    io_outb(PS2_PORT_CNTL, PS2_CNTL_WRITE_CFG);
    ps2_mouse_wait(PS2_STATUS_INDATA);
    io_outb(PS2_PORT_DATA, cfg);

    // No idea what is going on here
    ps2_mouse_write(PS2_MOUSECTL_PULSE6);
    ps2_mouse_read();
    ps2_mouse_write(PS2_MOUSECTL_PULSE4);
    ps2_mouse_read();

    io_outb(PS2_PORT_CNTL, PS2_CNTL_WRITE_MOUSE);
    io_outb(PS2_PORT_DATA, PS2_MOUSECTL_SAMPLERATE);
    u8 sample_rate = io_inb(PS2_PORT_DATA);
    if (sample_rate != PS2_MOUSECTL_PULSE10) {
        log_debug(
            "received unexpcted value for PS2_MOUSECTL_SAMPLERATE: expected "
            "%X, got %X",
            PS2_MOUSECTL_PULSE10, sample_rate);
    }

    io_outb(PS2_PORT_CNTL, PS2_CNTL_WRITE_MOUSE);
    io_outb(PS2_PORT_DATA, 10);
    sample_rate = io_inb(PS2_PORT_DATA);
    if (sample_rate != PS2_MOUSECTL_PULSE10) {
        log_debug(
            "received unexpcted value for PS2_MOUSECTL_SAMPLERATE: expected "
            "%X, got %X",
            PS2_MOUSECTL_PULSE10, sample_rate);
    }

    g_ps2_mouse.mouse = mouse_new();
    g_ps2_mouse.has_wheel = false;
    InterruptHandler *mouse_isr = interrupt_allocate(ps2_mouse_isr, lapic_eoi);
    mouse_isr->flags |= INTERRUPT_FLAGS_EOI_AFTER;
    ioapic_set_irq(IOAPIC_IRQ_MOUSE, mouse_isr->vec & 0xff,
                   cpu_get_hardware_id(), false);
}
