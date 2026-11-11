#pragma once

#include <kernel/com/ds/ringbuffer.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>

#define KEYBOARD_MOD_LCTRL  1
#define KEYBOARD_MOD_RCTRL  2
#define KEYBOARD_MOD_LSHIFT 4
#define KEYBOARD_MOD_RSHIFT 8
#define KEYBOARD_MOD_LALT   16
#define KEYBOARD_MOD_RALT   32
#define KEYBOARD_MOD_SHIFT  (KEYBOARD_MOD_LSHIFT | KEYBOARD_MOD_RSHIFT)

// TAKEN: Mathewnd/Astral
#define KEYBOARD_KEY_RESERVED       0
#define KEYBOARD_KEY_ESCAPE         1
#define KEYBOARD_KEY_1              2
#define KEYBOARD_KEY_2              3
#define KEYBOARD_KEY_3              4
#define KEYBOARD_KEY_4              5
#define KEYBOARD_KEY_5              6
#define KEYBOARD_KEY_6              7
#define KEYBOARD_KEY_7              8
#define KEYBOARD_KEY_8              9
#define KEYBOARD_KEY_9              10
#define KEYBOARD_KEY_0              11
#define KEYBOARD_KEY_MINUS          12
#define KEYBOARD_KEY_EQUAL          13
#define KEYBOARD_KEY_BACKSPACE      14
#define KEYBOARD_KEY_TAB            15
#define KEYBOARD_KEY_Q              16
#define KEYBOARD_KEY_W              17
#define KEYBOARD_KEY_E              18
#define KEYBOARD_KEY_R              19
#define KEYBOARD_KEY_T              20
#define KEYBOARD_KEY_Y              21
#define KEYBOARD_KEY_U              22
#define KEYBOARD_KEY_I              23
#define KEYBOARD_KEY_O              24
#define KEYBOARD_KEY_P              25
#define KEYBOARD_KEY_LEFTBRACE      26
#define KEYBOARD_KEY_RIGHTBRACE     27
#define KEYBOARD_KEY_ENTER          28
#define KEYBOARD_KEY_LEFTCTRL       29
#define KEYBOARD_KEY_A              30
#define KEYBOARD_KEY_S              31
#define KEYBOARD_KEY_D              32
#define KEYBOARD_KEY_F              33
#define KEYBOARD_KEY_G              34
#define KEYBOARD_KEY_H              35
#define KEYBOARD_KEY_J              36
#define KEYBOARD_KEY_K              37
#define KEYBOARD_KEY_L              38
#define KEYBOARD_KEY_SEMICOLON      39
#define KEYBOARD_KEY_APOSTROPHE     40
#define KEYBOARD_KEY_GRAVE          41
#define KEYBOARD_KEY_LEFTSHIFT      42
#define KEYBOARD_KEY_BACKSLASH      43
#define KEYBOARD_KEY_Z              44
#define KEYBOARD_KEY_X              45
#define KEYBOARD_KEY_C              46
#define KEYBOARD_KEY_V              47
#define KEYBOARD_KEY_B              48
#define KEYBOARD_KEY_N              49
#define KEYBOARD_KEY_M              50
#define KEYBOARD_KEY_COMMA          51
#define KEYBOARD_KEY_DOT            52
#define KEYBOARD_KEY_SLASH          53
#define KEYBOARD_KEY_RIGHTSHIFT     54
#define KEYBOARD_KEY_KEYPADASTERISK 55
#define KEYBOARD_KEY_LEFTALT        56
#define KEYBOARD_KEY_SPACE          57
#define KEYBOARD_KEY_CAPSLOCK       58
#define KEYBOARD_KEY_F1             59
#define KEYBOARD_KEY_F2             60
#define KEYBOARD_KEY_F3             61
#define KEYBOARD_KEY_F4             62
#define KEYBOARD_KEY_F5             63
#define KEYBOARD_KEY_F6             64
#define KEYBOARD_KEY_F7             65
#define KEYBOARD_KEY_F8             66
#define KEYBOARD_KEY_F9             67
#define KEYBOARD_KEY_F10            68
#define KEYBOARD_KEY_NUMLOCK        69
#define KEYBOARD_KEY_SCROLLLOCK     70
#define KEYBOARD_KEY_KEYPAD7        71
#define KEYBOARD_KEY_KEYPAD8        72
#define KEYBOARD_KEY_KEYPAD9        73
#define KEYBOARD_KEY_KEYPADMINUS    74
#define KEYBOARD_KEY_KEYPAD4        75
#define KEYBOARD_KEY_KEYPAD5        76
#define KEYBOARD_KEY_KEYPAD6        77
#define KEYBOARD_KEY_KEYPADPLUS     78
#define KEYBOARD_KEY_KEYPAD1        79
#define KEYBOARD_KEY_KEYPAD2        80
#define KEYBOARD_KEY_KEYPAD3        81
#define KEYBOARD_KEY_KEYPAD0        82
#define KEYBOARD_KEY_KEYPADDOT      83
#define KEYBOARD_KEY_F11            87
#define KEYBOARD_KEY_F12            88
#define KEYBOARD_KEY_KEYPADENTER    96
#define KEYBOARD_KEY_RIGHTCTRL      97
#define KEYBOARD_KEY_KEYPADSLASH    98
#define KEYBOARD_KEY_SYSREQ         99
#define KEYBOARD_KEY_RIGHTALT       100
#define KEYBOARD_KEY_LINEFEED       101
#define KEYBOARD_KEY_HOME           102
#define KEYBOARD_KEY_UP             103
#define KEYBOARD_KEY_PAGEUP         104
#define KEYBOARD_KEY_LEFT           105
#define KEYBOARD_KEY_RIGHT          106
#define KEYBOARD_KEY_END            107
#define KEYBOARD_KEY_DOWN           108
#define KEYBOARD_KEY_PAGEDOWN       109
#define KEYBOARD_KEY_INSERT         110
#define KEYBOARD_KEY_DELETE         111

#define KEYBOARD_KEYSTATE_PRESSED  1
#define KEYBOARD_KEYSTATE_RELEASED 2

typedef struct KbdPacket {
    struct Keyboard *keyboard;

    // Set by the keyboard driverj
    int keycode;
    int keystate;

    // Set by the layout driver
    // (used for kernel TTYs)
    char c;
} KbdPacket;

// Used by the layout driver to tell the input driver what to do with the
// returned packet
typedef enum KbdAction {
    KBD_ACTION_SEND,
    KBD_ACTION_DISCARD // for dead keys
} KbdAction;

typedef KbdAction (*KbdLayoutFn)(KbdPacket *pkt);

typedef struct Keyboard {
    KbdLayoutFn layout;
    Vnode *vnode;
    Ringbuffer rb;
    struct PollHead pollhead;

    // used by the layout driver to when to use uppercase
    struct {
        int mod;
        bool caps_lock;
    } state;
} Keyboard;

int keyboard_send_packet(Keyboard *keyboard, KbdPacket *pkt);
Keyboard *keyboard_new(KbdLayoutFn layout);
void keyboard_update_state(KbdPacket *pkt);

// LAYOUTS

KbdAction keyboard_layout_en_us(KbdPacket *pkt);
