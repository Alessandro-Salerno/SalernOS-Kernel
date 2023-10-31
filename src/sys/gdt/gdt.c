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

#include "kstdio.h"
#include "sys/gdt/gdt.h"

typedef struct GDTDescriptor {
  uint16_t _Limit;
  uint16_t _BaseLow;
  uint8_t  _BaseMid;
  uint8_t  _Access;
  uint8_t  _Granularity;
  uint8_t  _BaseHigh;
} gdtdesc_t;

typedef struct TssDescriptor {
  uint16_t _Length;
  uint16_t _BaseLow;
  uint8_t  _BaseMid;
  uint8_t  _Flags1;
  uint8_t  _Flags2;
  uint8_t  _BaseHigh;
  uint32_t _BaseUpper;
  uint32_t _Reserved;
} tssdesc_t;

typedef struct GDT {
  gdtdesc_t _Descriptors[11];
  tssdesc_t _TSS;
} gdt_t;

typedef struct __attribute__((packed)) GDTR {
  uint16_t _Limit;
  uint64_t _Base;
} gdtr_t;

static gdt_t  gdt;
static gdtr_t gdtr;

static inline void __new_segment__(uint64_t __offset,
                                   uint16_t __limit,
                                   uint16_t __baselow,
                                   uint8_t  __basemid,
                                   uint8_t  __access,
                                   uint8_t  __granularity,
                                   uint8_t  __basehigh) {
  gdt._Descriptors[__offset]._Limit       = __limit;
  gdt._Descriptors[__offset]._BaseLow     = __baselow;
  gdt._Descriptors[__offset]._BaseMid     = __basemid;
  gdt._Descriptors[__offset]._Access      = __access;
  gdt._Descriptors[__offset]._Granularity = __granularity;
  gdt._Descriptors[__offset]._BaseHigh    = __basehigh;
}

void sys_gdt_init() {
  // Null segment
  kloginfo("GDT: Loading null segment");
  __new_segment__(0, 0, 0, 0, 0, 0, 0);

  // 16 bit kernel code segment
  kloginfo("GDT: Loading kernel code (16) segment");
  __new_segment__(1, 0xffff, 0, 0, 0b10011010, 0, 0);

  // 16 bit kernel data segment
  kloginfo("GDT: Loading kernel data (16) segment");
  __new_segment__(2, 0xffff, 0, 0, 0b10010010, 0, 0);

  // 32 bit kernel code segment
  kloginfo("GDT: Loading kernel code (32) segment");
  __new_segment__(3, 0xffff, 0, 0, 0b10011010, 0b11001111, 0);

  // 32 bit kernel data segment
  kloginfo("GDT: Loading kernel data (32) segment");
  __new_segment__(4, 0xffff, 0, 0, 0b10010010, 0b11001111, 0);

  // 64 bit kernel code segment
  kloginfo("GDT: Loading kernel code (64) segment");
  __new_segment__(5, 0xffff, 0, 0, 0b10011010, 0b00100000, 0);

  // 64 bit kernel data segment
  kloginfo("GDT: Loading kernel data (64) segment");
  __new_segment__(6, 0xffff, 0, 0, 0b10010010, 0, 0);

  // System call segments
  kloginfo("GDT: Loading SCE segment");
  gdt._Descriptors[7] = (gdtdesc_t){0};
  gdt._Descriptors[8] = (gdtdesc_t){0};

  // 64 bit user code segment
  kloginfo("GDT: Loading user code (64) segment");
  __new_segment__(9, 0xffff, 0, 0, 1, 0, 0);

  // 64 bit user data segment
  kloginfo("GDT: Loading user data (64) segment");
  __new_segment__(10, 0xffff, 0, 0, 1, 0, 0);

  // Task State Segment
  kloginfo("GDT: Loading task state segment (TSS)");
  gdt._TSS._Length    = sizeof(gdt_t); // REmiNDER: 104
  gdt._TSS._BaseLow   = 0;
  gdt._TSS._BaseMid   = 0;
  gdt._TSS._Flags1    = 0b10001001;
  gdt._TSS._Flags2    = 0;
  gdt._TSS._BaseHigh  = 0;
  gdt._TSS._BaseUpper = 0;
  gdt._TSS._Reserved  = 0;

  // Load GDTR
  kloginfo("GDT: Loading GDTR");
  gdtr._Limit = sizeof(gdt_t) - 1;
  gdtr._Base  = (uint64_t)&gdt;

  kloginfo("GDT: Applying changes...");
  sys_gdt_reload();
  klogok("GDT: Done");
}

void sys_gdt_reload() {
  // NOTE: Temporary code
  // Borrowed from Lyre Kernel
  asm volatile("lgdt %0\n\t"
               "push $0x28\n\t"
               "lea 1f(%%rip), %%rax\n\t"
               "push %%rax\n\t"
               "lretq\n\t"
               "1:\n\t"
               "mov $0x30, %%ax\n\t"
               "mov %%ax, %%ds\n\t"
               "mov %%ax, %%es\n\t"
               "mov %%ax, %%fs\n\t"
               "mov %%ax, %%gs\n\t"
               "mov %%ax, %%ss\n\t"
               :
               : "m"(gdtr)
               : "rax", "memory");
}
