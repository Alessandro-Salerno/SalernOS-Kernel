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

// #define DISABLE_LOGGING

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

#define KNANOS_PER_SEC 1000000000UL
#define KFPS(fps)      (KNANOS_PER_SEC / (fps))

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_URGENT
#define KURGENT(...)                          \
    com_io_log_acquire();                     \
    kinitlog("URGENT", "\033[35m");           \
    com_io_log_puts(__FILE__ ":");            \
    com_io_log_puts(__func__);                \
    com_io_log_puts(":" KSTR(__LINE__) ": "); \
    kprintf(__VA_ARGS__);                     \
    com_io_log_putc('\n');                    \
    com_io_log_release();
#endif

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_INFO
#define KLOG(...)                             \
    com_io_log_acquire();                     \
    kinitlog("INFO", "\033[32m");             \
    com_io_log_puts(__FILE__ ":");            \
    com_io_log_puts(__func__);                \
    com_io_log_puts(":" KSTR(__LINE__) ": "); \
    kprintf(__VA_ARGS__);                     \
    com_io_log_putc('\n');                    \
    com_io_log_release();
#else
#define KLOG(...)
#endif

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_DEBUG
#define KDEBUG(...)                           \
    com_io_log_acquire();                     \
    kinitlog("DEBUG", "");                    \
    com_io_log_puts(__FILE__ ":");            \
    com_io_log_puts(__func__);                \
    com_io_log_puts(":" KSTR(__LINE__) ": "); \
    kprintf(__VA_ARGS__);                     \
    com_io_log_putc('\n');                    \
    com_io_log_release();
#else
#define KDEBUG(...)
#endif

#if CONFIG_ASSERT_ACTION == CONST_ASSERT_REMOVE
#define KASSERT(statement)
#elif CONFIG_ASSERT_ACTION == CONST_ASSERT_EXPAND
#define KASSERT(statement) (statement)
#elif CONFIG_ASSERT_ACTION == CONST_ASSERT_SOFT
#define KASSERT(statement)                                               \
    if (KUNKLIKELY(!(statement))) {                                      \
        com_io_log_acquire();                                            \
        kinitlog("ASSERT", "");                                          \
        com_io_log_puts(__FILE__ ":");                                   \
        com_io_log_puts(__func__);                                       \
        com_io_log_puts(":" KSTR(__LINE__) ": " #statement " failed\n"); \
        com_io_log_release();                                            \
    }
#elif CONFIG_ASSERT_ACTION == CONST_ASSERT_PANIC
#define KASSERT(statement)                                               \
    if (KUNKLIKELY(!(statement))) {                                      \
        com_io_log_acquire();                                            \
        kinitlog("ASSERT", "");                                          \
        com_io_log_puts(__FILE__ ":");                                   \
        com_io_log_puts(__func__);                                       \
        com_io_log_puts(":" KSTR(__LINE__) ": " #statement " failed\n"); \
        com_panic(NULL, NULL);                                           \
    }
#else
#error "unsupported assert action, check config/config.h"
#endif

void kinitlog(const char *category, const char *color);
