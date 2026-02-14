/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
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

#include <arch/info.h>
#include <kernel/com/io/log.h>

#define KSTR_HELPER(x) #x
#define KSTR(x)        KSTR_HELPER(x)

#define KLIKELY(x)    __builtin_expect(!!(x), 1)
#define KUNKLIKELY(x) __builtin_expect(!!(x), 0)

#define KMIN(x, y) (((x) < (y)) ? (x) : (y))
#define KMAX(x, y) (((x) > (y)) ? (x) : (y))

#define KUSER_TEXT __attribute__((section(".user_text")))
#define KUSER_DATA __attribute__((section(".user_data")))
#define KUSED      __attribute__((used))

#define KNANOS_PER_SEC 1000000000UL
#define KFPS(fps)      (KNANOS_PER_SEC / (fps))

// NOTE: only works if multiple is a power of 2
#define KMOD_FAST(value, multiple) ((value) & ((multiple) - 1))

#define KCACHE_FRIENDLY __attribute__((aligned(ARCH_CACHE_ALIGN)))

#if CONFIG_LOG_LOCATION == CONST_LOG_LOCATION_NONE
#define __LOG_LOCATION()
#elif CONFIG_LOG_LOCATION == CONST_LOG_LOCATION_ALL
#define __LOG_LOCATION()                  \
    com_io_log_puts_nolock(__FILE__ ":"); \
    com_io_log_puts_nolock(__func__);     \
    com_io_log_puts_nolock(":" KSTR(__LINE__) ": ");
#endif

#if CONFIG_LOG_ALLOW_COLORS
#define KRESET_COLOR() com_io_log_puts_nolock("\033[0m")
#else
#define KRESET_COLOR()
#endif

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_URGENT
#define KURGENT(...)                \
    com_io_log_lock();              \
    kinitlog("URGENT", "\033[35m"); \
    __LOG_LOCATION();               \
    printf(__VA_ARGS__);            \
    KRESET_COLOR();                 \
    com_io_log_putc_nolock('\n');   \
    com_io_log_unlock();
#endif

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_INFO
#define KLOG(...)                 \
    com_io_log_lock();            \
    kinitlog("INFO", "\033[32m"); \
    __LOG_LOCATION();             \
    printf(__VA_ARGS__);          \
    KRESET_COLOR();               \
    com_io_log_putc_nolock('\n'); \
    com_io_log_unlock();
#else
#define KLOG(...)
#endif

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_OPTION
#define KOPTMSG(optname, ...)      \
    com_io_log_lock();             \
    kinitlog(optname, "\033[94m"); \
    __LOG_LOCATION();              \
    printf(__VA_ARGS__);           \
    KRESET_COLOR();                \
    com_io_log_unlock();
#else
#define KOPTMSG(...)
#endif

#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_DEBUG
#define KDEBUG(...)               \
    com_io_log_lock();            \
    kinitlog("DEBUG", "");        \
    __LOG_LOCATION();             \
    printf(__VA_ARGS__);          \
    KRESET_COLOR();               \
    com_io_log_putc_nolock('\n'); \
    com_io_log_unlock();
#else
#define KDEBUG(...)
#endif

#if CONFIG_ASSERT_ACTION == CONST_ASSERT_REMOVE
#define KASSERT(statement)
#define KASSERT_CALL(retval, eq, fn) fn
#define KASSERT_CALL_SUCCESSFUL(fn)  fn

#elif CONFIG_ASSERT_ACTION == CONST_ASSERT_EXPAND
#define KASSERT(statement)           (statement)
#define KASSERT_CALL(retval, eq, fn) fn
#define KASSERT_CALL_SUCCESSFUL(fn)  fn

#elif CONFIG_ASSERT_ACTION == CONST_ASSERT_SOFT
#define KASSERT(statement)                                        \
    if (KUNKLIKELY(!(statement))) {                               \
        com_io_log_lock();                                        \
        kinitlog("ASSERT", "");                                   \
        com_io_log_puts_nolock(__FILE__ ":");                     \
        com_io_log_puts_nolock(__func__);                         \
        com_io_log_puts_nolock(":" KSTR(__LINE__) ": " #statement \
                                                  " failed\n");   \
        com_io_log_unlock();                                      \
    }
#define KASSERT_CALL(retval, eq, fn) KASSERT(retval eq fn)
#define KASSERT_CALL_SUCCESSFUL(fn)  KASSERT(fn)

#elif CONFIG_ASSERT_ACTION == CONST_ASSERT_PANIC
#define KASSERT(statement)                                        \
    if (KUNKLIKELY(!(statement))) {                               \
        com_io_log_lock();                                        \
        KRESET_COLOR();                                           \
        kinitlog("ASSERT", "");                                   \
        com_io_log_puts_nolock(__FILE__ ":");                     \
        com_io_log_puts_nolock(__func__);                         \
        com_io_log_puts_nolock(":" KSTR(__LINE__) ": " #statement \
                                                  " failed\n");   \
        com_sys_panic(NULL,                                       \
                      "assertion failed: %s:%s:%zu: %s",          \
                      __FILE__,                                   \
                      __func__,                                   \
                      __LINE__,                                   \
                      #statement);                                \
    }
#define KASSERT_CALL(retval, eq, fn) KASSERT(retval eq fn)
#define KASSERT_CALL_SUCCESSFUL(fn)  KASSERT(fn)

#else
#error "unsupported assert action, check config/config.h"
#endif

void kinitlog(const char *category, const char *color);
