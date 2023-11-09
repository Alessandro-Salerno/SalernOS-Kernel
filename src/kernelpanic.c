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
#include <kstdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "kernelpanic.h"
#include "sys/cpu/ctx.h"

void panic_throw(const char *__fmt, cpuctx_t *__cpuctx, ...) {
  asm volatile("sti");
  klogerr("Kernel panic: execution of the kernel has been halted due to a "
          "critical system fault.");
  va_list _arguments;
  va_start(_arguments, __cpuctx);

  kvprintf(__fmt, _arguments);
  kprintf("\n\n");

  if (__cpuctx != NULL) {
    kprintf("[Registers]:\n");
    kprintf("RBP:\t%u\nRDI:\t%u\n", __cpuctx->_RBP, __cpuctx->_RDI);
    kprintf("RSI:\t%u\nRDX:\t%u\n", __cpuctx->_RSI, __cpuctx->_RDX);
    kprintf("RCX:\t%u\nRBX:\t%u\n", __cpuctx->_RCX, __cpuctx->_RBX);
    kprintf("RAX:\t%u\n\n", __cpuctx->_RAX);

    kprintf("[Numbered Registers]:\n");
    kprintf("R15:\t%u\nR14:\t%u\n", __cpuctx->_R15, __cpuctx->_R14);
    kprintf("R13:\t%u\nR12:\t%u\n", __cpuctx->_R13, __cpuctx->_R12);
    kprintf("R11:\t%u\nR10:\t%u\n", __cpuctx->_R11, __cpuctx->_R10);
    kprintf("R09:\t%u\nR08:\t%u\n\n", __cpuctx->_R9, __cpuctx->_R8);

    kprintf("[Special Registers]:\n");
    kprintf("RIP:\t%u\nRSP:\t%u\n", __cpuctx->_RIP, __cpuctx->_RSP);
    kprintf("CS:\t%u\nSS:\t%u\n", __cpuctx->_CS, __cpuctx->_SS);
    kprintf("RFlags:\t%u\n", __cpuctx->_RFlags);
  }

  while (true)
    asm volatile("hlt");
}

void panic_assert(uint8_t __cond, const char *__message) {
  SOFTASSERT(!(__cond), RETVOID);
  panic_throw(__message, NULL);
}
