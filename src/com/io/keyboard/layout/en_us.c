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

KbdAction keyboard_layout_en_us(KbdPacket *pkt) {
    Keyboard *keyboard = pkt->keyboard;

    if (pkt->keycode >= 0x80) return KBD_ACTION_DISCARD;

    bool shift = keyboard->state.mod & KEYBOARD_MOD_SHIFT;
    if (shift ^ keyboard->state.caps_lock) {
        pkt->c = G_LAYOUT_EN_US_MAP_SHIFTED[pkt->keycode];
    } else {
        pkt->c = G_LAYOUT_EN_US_MAP[pkt->keycode];
    }

    return KBD_ACTION_SEND;
}
