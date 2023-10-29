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

#include "sys/syscall/dispatcher.h"
#include <kstdio.h>

#include "kernelpanic.h"
#include "sys/syscall/syscalls.h"

static void (*syscallHandlers[])(void *__frmae) = {syscall_handlers_printstr};

void syscall_dispatch(void *__frame, uint32_t __syscall) {
  panic_assert(__syscall < sizeof(syscallHandlers) / sizeof(void (*)(void *)),
               "Syscall invoked with invalid ID!");

#ifdef DEBUG
  kprintf("\n\nSYSTEM CALL INFORMATION:\nFrame Poiinter: %u\nSyscall ID: %d\n",
          (uint64_t)(__frame),
          __syscall);
#endif

  syscallHandlers[__syscall](__frame);
}
