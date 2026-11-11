#pragma once

#include <kernel/com/util.h>

// verify always evaluates its argument, assert may not
#if CONFIG_ASSERT_ACTION == CONST_ASSERT_REMOVE
#define assert(e) ((void)0)
#define verify(e) ((void)(e))
#elif CONFIG_ASSERT_ACTION == CONST_ASSERT_EXPAND
#define assert(e) ((void)(e))
#define verify(e) ((void)(e))
#elif CONFIG_ASSERT_ACTION == CONST_ASSERT_SOFT || \
    CONFIG_ASSERT_ACTION == CONST_ASSERT_PANIC
#define assert(e) \
    (likely(e) ? (void)0 : assert_fail(#e, __FILE__, __func__, __LINE__))
#define verify(e) assert(e)
#else
#error "unsupported assert action, check config/config.h"
#endif

#if CONFIG_ASSERT_ACTION == CONST_ASSERT_PANIC
noreturn
#endif
    void assert_fail(const char *expr, const char *file, const char *func,
                     int line);
