/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2023 Alessandro Salerno

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**********************************************************************/

#include <kdebug.h>
#include <stdint.h>

#include "kernelpanic.h"
#include "sys/cpu/ctx.h"
#include "sys/except/except.h"
#include "sys/idt/idt.h"

static const char *exceptions[] = {/* 0x0e */ "Division by zero exception",
                                   /* 0x0f */ "Debug",
                                   /* 0x10 */ "Non-maskable interrupt",
                                   /* 0x11 */ "Breakpoint hit",
                                   /* 0x12 */ "Overflow",
                                   /* 0x13 */ "Bound range exceeded",
                                   /* 0x14 */ "Invalid opcode",
                                   /* 0x15 */ "Device not available",
                                   /* 0x16 */ "Double fault",
                                   /* 0x17 */ "???",
                                   /* 0x18 */ "Invalid TSS",
                                   /* 0x19 */ "Segment not present",
                                   /* 0x1a */ "Stack-segment fault",
                                   /* 0x1b */ "General protection fault",
                                   /* 0x1c */ "Page fault",
                                   /* 0x1d */ "???",
                                   /* 0x1e */ "x87 exception",
                                   /* 0x1f */ "Alignment check",
                                   /* 0x20 */ "Machine check",
                                   /* 0x21 */ "SIMD exception",
                                   /* 0x22 */ "Virtualisation"};

static void __handle_exception__(uint8_t __vec, cpuctx_t *__cpuctx) {
  panic_throw("Exception: %s", __cpuctx, exceptions[__vec]);
}

void sys_except_initialize() {
  kloginfo("Except: Initializing exceptions");

  for (int _i = 0; _i < sizeof(exceptions) / sizeof(char *); _i++) {
    sys_idt_isr_set(_i, __handle_exception__);
  }

  sys_idt_ist_set(0x0e, 2);
  klogok("Except: Done!");
}
