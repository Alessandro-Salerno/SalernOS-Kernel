/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
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

#define DISABLE_LOGGING

#include <kernel/com/io/log.h>

#define KSTR_HELPER(x) #x
#define KSTR(x)        KSTR_HELPER(x)

#define KLIKELY(x)    __builtin_expect(!!(x), 1)
#define KUNKLIKELY(x) __builtin_expect(!!(x), 0)

#define KMIN(x, y) (((x) < (y)) ? (x) : (y))
#define KMAX(x, y) (((x) > (y)) ? (x) : (y))

#define USER_TEXT __attribute__((section(".user_text")))
#define USER_DATA __attribute__((section(".user_data")))
#define USED      __attribute__((used))

#ifndef DISABLE_LOGGING

#define KLOG(...)                             \
    com_io_log_acquire();                     \
    com_io_log_puts("[  log  ] ");            \
    com_io_log_puts(__FILE__ ":");            \
    com_io_log_puts(__func__);                \
    com_io_log_puts(":" KSTR(__LINE__) ": "); \
    kprintf(__VA_ARGS__);                     \
    com_io_log_putc('\n');                    \
    com_io_log_release();

#define KDEBUG(...)                           \
    com_io_log_acquire();                     \
    com_io_log_puts("[ debug ] ");            \
    com_io_log_puts(__FILE__ ":");            \
    com_io_log_puts(__func__);                \
    com_io_log_puts(":" KSTR(__LINE__) ": "); \
    kprintf(__VA_ARGS__);                     \
    com_io_log_putc('\n');                    \
    com_io_log_release();
#else
#define KLOG(...)
#define KDEBUG(...)
#endif

// NOTE: this does not release the lock because noone else should print
// afterwards
#define KASSERT(statement)                                               \
    if (KUNKLIKELY(!(statement))) {                                      \
        com_io_log_acquire();                                            \
        com_io_log_puts(__FILE__ ":");                                   \
        com_io_log_puts(__func__);                                       \
        com_io_log_puts(":" KSTR(__LINE__) ": " #statement " failed\n"); \
        com_panic(NULL, NULL);                                           \
    }
