#pragma once

#include <kernel/com/util.h>

void tsc_boot(void);
void tsc_bsp_init(void);
void tsc_init(void);

static inline u64 tsc_read(void) {
    u32 high;
    u32 low;
    asm volatile("rdtscp" : "=a"(low), "=d"(high) : : "rcx");
    return ((u64)high << 32) | low;
}

static inline u64 tsc_val_to_ns(u64 tsc_reverse_mult, u64 val) {
    return (u64)(((__uint128_t)val * tsc_reverse_mult) >> 32);
}

static inline u64 tsc_get_ns(u64 tsc_reverse_mult) {
    extern u64 g_tsc_boot_time;
    return tsc_val_to_ns(tsc_reverse_mult, tsc_read()) - g_tsc_boot_time;
}
