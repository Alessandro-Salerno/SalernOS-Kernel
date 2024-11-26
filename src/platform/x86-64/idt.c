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
#include <kernel/platform/x86-64/idt.h>
#include <stdint.h>

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

void x86_64_idt_reload() {
  LOG("reloading idt");
  asm volatile("lidt (%%rax)" : : "a"(&Idtr));
  // TODO: implement rest of this function
}
