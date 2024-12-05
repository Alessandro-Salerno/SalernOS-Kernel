/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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

#include <kernel/com/log.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/platform/x86-64/idt.h>
#include <stdint.h>

#include "arch/context.h"
#include "com/panic.h"

typedef struct {
  uint16_t size;
  uint64_t offset;
} __attribute__((packed)) idtr_t;

typedef struct {
  uint16_t offset;
  uint16_t segment;
  uint8_t  ist;
  uint8_t  flags;
  uint16_t offset2;
  uint32_t offset3;
  uint32_t reserved;
} __attribute__((packed)) idtentry_t;

static idtentry_t     Idt[256];
static idtr_t         Idtr = {.size = sizeof(Idt) - 1, .offset = (uint64_t)Idt};
extern const uint64_t IsrTable[256];

static char *Exceptions[] = {"division by 0",
                             "debug",
                             "mmi",
                             "breakpoint",
                             "overflow",
                             "bound range exceeded",
                             "invalid opcode",
                             "device not available",
                             "double fault",
                             "coprocessor segment overrun",
                             "invalid tss",
                             "segment not present",
                             "stack-segment fault",
                             "general protection fault",
                             "page fault",
                             "unknown",
                             "x87 floating-point exception",
                             "alignment check",
                             "machine check",
                             "simd exception"};

static void generic_exception_isr(com_isr_t *isr, arch_context_t *ctx) {
  if ((isr->id & 0xff) < 19) {
    com_panic(ctx, "cpu exception (%s)", Exceptions[isr->id & 0xff]);
  }

  com_panic(ctx, "cpu exception (unknown)");
}

void x86_64_idt_init() {
  LOG("initializing idt");

  for (uint64_t i = 0; i < 256; i++) {
    Idt[i].offset  = IsrTable[i] & 0xffff;
    Idt[i].segment = 0x08; // 64-bit GDT selector for kernel code
    Idt[i].ist     = 0;
    Idt[i].flags   = 0x8E;
    Idt[i].offset2 = (IsrTable[i] >> 16) & 0xffff;
    Idt[i].offset3 = (IsrTable[i] >> 32) & 0xffffffff;
  }
}

void x86_64_idt_set_user_invocable(uintmax_t vec) {
  DEBUG("allowing user invocation of interrupt %u", vec);
  Idt[vec].flags = 0xEE;
}

void x86_64_idt_reload() {
  LOG("reloading idt");
  asm volatile("lidt (%%rax)" : : "a"(&Idtr));

  for (uintmax_t i = 0; i < 32; i++) {
    com_sys_interrupt_register(i, generic_exception_isr, NULL);
  }

  com_sys_interrupt_set(true);
}
