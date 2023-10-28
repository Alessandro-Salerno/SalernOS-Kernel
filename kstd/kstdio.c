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

#include <kstdio.h>
#include <kstr.h>
#include <stdarg.h>

#include "termbind.h"

void kvprintf(const char *__fmt, va_list __args) {
  for (char *_ptr = (char *)(__fmt); *_ptr; _ptr++) {
    switch (*_ptr) {
    case '%': {
      switch (*(++_ptr)) {
      case 'u':
        kernel_terminal_fastwrite(
            (const char *)(uitoa(va_arg(__args, uint64_t))));
        break;

      case 'd':
      case 'i':
        kernel_terminal_fastwrite(
            (const char *)(itoa(va_arg(__args, int64_t))));
        break;

      case 'c':
        kernel_terminal_putchar((char)(va_arg(__args, signed)));
        break;

      case 's':
        kernel_terminal_fastwrite((const char *)(va_arg(__args, char *)));
        break;
      }

      break;
    }

    case '\n':
      kernel_terminal_write("\n");
      break;

    case '\t':
      kernel_terminal_write("\t");
      break;

    default:
      kernel_terminal_putchar(*_ptr);
      break;
    }
  }

  kernel_terminal_flush();
}

void kprintf(const char *__fmt, ...) {
  va_list _arguments;
  va_start(_arguments, __fmt);
  kvprintf(__fmt, _arguments);
  va_end(_arguments);
}
