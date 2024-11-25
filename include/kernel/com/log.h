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

#pragma once

#include <kernel/com/panic.h>
#include <kernel/com/util.h>
#include <stddef.h>

#define ASSERT(statement)                                        \
  if (UNLIKELY(!(statement))) {                                  \
    com_log_puts(__FILE__ ":");                                  \
    com_log_puts(__func__);                                      \
    com_log_puts(":" STR(__LINE__) ": " #statement " failed\n"); \
    com_panic(NULL, NULL);                                       \
  }

typedef void (*com_log_hook_t)(char);

void com_log_set_hook(com_log_hook_t hook);
void com_log_putc(char c);
void com_log_puts(const char *s);
void com_log_init();
