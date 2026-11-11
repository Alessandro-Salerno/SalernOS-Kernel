#pragma once

#include <kernel/com/io/keyboard.h>
#include <kernel/com/io/mouse.h>
#include <kernel/com/io/tty.h>
#include <kernel/com/util.h>

void console_kbd_in(KbdPacket *pkt);
void console_mouse_in(MousePacket *pkt);
void console_add_tty(Tty *tty);
bool console_switch(usize tty_idx);
bool console_rel(Tty *tty, bool allow);
bool console_is_foreground(Tty *tty);
bool console_wait_foreground(Tty *tty);
void console_init(void);
