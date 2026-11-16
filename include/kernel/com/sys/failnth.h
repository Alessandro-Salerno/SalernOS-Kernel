#pragma once

#include <arch/cpu.h>
#include <kernel/com/sys/thread.h>

#if CONFIG_FAIL_NTH
static inline bool failnth_should_fail(void) {
    Thread *thread = cpu_get_thread();
    if (thread == NULL || thread->fail_nth == 0) return false;
    return --thread->fail_nth == 0;
}

static inline void failnth_set(usize n) { cpu_get_thread()->fail_nth = n; }
static inline usize failnth_get(void) { return cpu_get_thread()->fail_nth; }
#else
static inline bool failnth_should_fail(void) { return false; }
#endif
