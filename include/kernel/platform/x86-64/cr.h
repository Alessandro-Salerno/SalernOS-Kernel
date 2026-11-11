#pragma once

#include <kernel/com/util.h>

static inline u64 cr_r0(void) {
    u64 val;
    asm volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

static inline u64 cr_r2(void) {
    u64 val;
    asm volatile("mov %%cr2, %0" : "=r"(val));
    return val;
}

static inline u64 cr_r4(void) {
    u64 val;
    asm volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

static inline void cr_w0(u64 val) {
    asm volatile("mov %0, %%cr0" : : "r"(val));
}

static inline void cr_w4(u64 val) {
    asm volatile("mov %0, %%cr4" : : "r"(val));
}
