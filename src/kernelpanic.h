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

#ifndef SALERNOS_CORE_KERNEL_PANIC
#define SALERNOS_CORE_KERNEL_PANIC

#include "Interrupts/handlers.h"
#include <kerntypes.h>

#define SOFTASSERT(__cond, __ret) \
  if (!(__cond))                  \
    return __ret;

typedef struct KernelPanicInfo {
  const char *_Type;
  const char *_Fault;
  const char *_Message;
} panicinfo_t;

/***************************************************************************************
RET TYPE        FUNCTION NAME           FUNCTION ARGUMENTS
***************************************************************************************/
void kernel_panic_throw(const char *__message, intframe_t *__regstate);
void kernel_panic_assert(uint8_t __cond, const char *__message);

#endif
