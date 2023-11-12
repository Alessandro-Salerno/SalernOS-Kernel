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
#include <stddef.h>
#include <stdint.h>

#include "kernelpanic.h"
#include "kstdio.h"
#include "sched/lock.h"
#include "sys/cpu/ctx.h"
#include "sys/idt/idt.h"
#include "sys/idt/ipi.h"

typedef struct IDTEntry {
  uint16_t _OffsetLow;
  uint16_t _Selector;
  uint8_t  _IST;
  uint8_t  _Flags;
  uint16_t _OffsetMid;
  uint32_t _OffsetHigh;
  uint32_t _Reserved;
} idtentry_t;

typedef struct __attribute__((packed)) IDTR {
  uint16_t _Liimt;
  uint64_t _Base;
} idtr_t;

static idtentry_t idt[256];

void *isrLookup[256];

static void __generic_isr__(uint8_t __vec, cpuctx_t *__cputctx) {
  panic_throw(
      "Unassigned interrupt vector invoke %u", __cputctx, (uint64_t)__vec);
}

void sys_idt_isr_register(uint8_t __vec, void *__isr, uint8_t __flags) {
  uint64_t _isr_addr = (uint64_t)__isr;

  idt[__vec] = (idtentry_t){._OffsetLow  = (uint16_t)_isr_addr,
                            ._Selector   = 0x28,
                            ._IST        = 0,
                            ._Flags      = __flags,
                            ._OffsetMid  = (uint16_t)(_isr_addr >> 16),
                            ._OffsetHigh = (uint32_t)(_isr_addr >> 32),
                            ._Reserved   = 0};
}

uint8_t sys_idt_vector_allocate() {
  static lock_t  _vec_lock = SCHED_LOCK_NEW();
  static uint8_t _vec      = 32;

  sched_lock_acquire(&_vec_lock);

  if (_vec == 0xf0) {
    panic_throw("Kernel ran out of space in x86-64 IDT", NULL);
  }

  uint8_t _ret = _vec++;

  sched_lock_release(&_vec_lock);
  return _ret;
}

void sys_idt_isr_set(uint8_t __vec, void *__isr) {
  isrLookup[__vec] = __isr;
}

void *sys_idt_isr_get(uint8_t __vec) {
  return isrLookup[__vec];
}

void sys_idt_ist_set(uint8_t __vec, uint8_t __ist) {
  idt[__vec]._IST = __ist;
}

uint8_t sys_idt_ist_get(uint8_t __vec) {
  return idt[__vec]._IST;
}

void sys_idt_flags_set(uint8_t __vec, uint8_t __flags) {
  idt[__vec]._Flags = __flags;
}

uint8_t sys_idt_flags_get(uint8_t __vec) {
  return idt[__vec]._Flags;
}

void sys_idt_reload() {
  idtr_t _idtr = (idtr_t){._Liimt = sizeof(idt) - 1, ._Base = (uint64_t)&idt};
  asm volatile("lidt %0" ::"m"(_idtr) : "memory");
}

void sys_idt_initialize() {
  kloginfo("IDT: Loading generic ISRs...");

  for (uint64_t _i = 0; _i < 256; _i++) {
    sys_idt_isr_register(_i, isrThunkLookup[_i], 0x8e);
    sys_idt_isr_set(_i, __generic_isr__);
  }

  kloginfo("IDT: Loading IPI entries...");
  sys_idt_ipi_initialize();

  kloginfo("IDT: Applying changes...");
  sys_idt_reload();
  klogok("IDT: Done!");
}
