/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2022 Alessandro Salerno

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

#ifndef SALERNOS_CORE_KERNEL_INTERRUPT_HANDLERS
#define SALERNOS_CORE_KERNEL_INTERRUPT_HANDLERS

#include <kerntypes.h>

#define ISR __attribute__((interrupt))

typedef struct __attribute__((packed)) InterruptFrame {
  uint64_t _R15;
  uint64_t _R14;
  uint64_t _R13;
  uint64_t _R12;
  uint64_t _R11;
  uint64_t _R10;
  uint64_t _R9;
  uint64_t _R8;
  uint64_t _RBP;
  uint64_t _RDI;
  uint64_t _RSI;
  uint64_t _RDX;
  uint64_t _RCX;
  uint64_t _RBX;
  uint64_t _RAX;
  uint64_t _ReservedZero;
  uint64_t _ResrvedOne;
  uint64_t _RIP;
  uint64_t _CS;
  uint64_t _RFlags;
  uint64_t _RSP;
  uint64_t _SS;
} intframe_t;

ISR void kernel_interrupt_handlers_pgfault(intframe_t *__frame);
ISR void kernel_interrupt_handlers_dfault(intframe_t *__frame);
ISR void kernel_interrupt_handlers_gpfault(intframe_t *__frame);
ISR void kernel_interrupt_handlers_kbhit(intframe_t *__frame);
ISR void kernel_interrupt_handlers_tick(intframe_t *__frame);

#endif
