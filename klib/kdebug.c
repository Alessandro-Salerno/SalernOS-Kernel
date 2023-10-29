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
#include <stdarg.h>

#include "kdebug.h"
#include "termbind.h"

#define INFO_FGCOLOR 0x00ffffff
#define INFO_TEXT    "[  DEBUG  ]"

#define ERR_FGCOLOR 0x00ff0000
#define ERR_TEXT    "[  ERROR  ]"

#define WARN_FGCOLOR 0x00ffff00
#define WARN_TEXT    "[ WARNING ]"

#define OK_FGCOLOR 0x0000ff00
#define OK_TEXT    "[ SUCCESS ]"

static void __putmsg__(uint32_t    __color,
                       const char *__type,
                       const char *__fmt,
                       va_list     __args) {
  uint32_t _color, _backcolor;

  kernel_terminal_info_get(&_backcolor, &_color);
  kernel_terminal_info_set(_backcolor, __color);

  kprintf("%s\t", __type);
  kvprintf(__fmt, __args);

  kernel_terminal_info_set(_backcolor, _color);

  kprintf("\n");
}

void kloginfo(const char *__fmt, ...) {
  va_list _args;
  va_start(_args, __fmt);
  __putmsg__(INFO_FGCOLOR, INFO_TEXT, __fmt, _args);
  va_end(_args);
}

void klogerr(const char *__fmt, ...) {
  va_list _args;
  va_start(_args, __fmt);
  __putmsg__(ERR_FGCOLOR, ERR_TEXT, __fmt, _args);
  va_end(_args);
}

void klogwarn(const char *__fmt, ...) {
  va_list _args;
  va_start(_args, __fmt);
  __putmsg__(WARN_FGCOLOR, WARN_TEXT, __fmt, _args);
  va_end(_args);
}

void klogok(const char *__fmt, ...) {
  va_list _args;
  va_start(_args, __fmt);
  __putmsg__(OK_FGCOLOR, OK_TEXT, __fmt, _args);
  va_end(_args);
}
