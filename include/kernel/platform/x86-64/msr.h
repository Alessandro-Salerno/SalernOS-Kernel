#pragma once

#include <kernel/com/util.h>

#define MSR_EFER         0xC0000080
#define MSR_STAR         0xC0000081
#define MSR_LSTAR        0xC0000082
#define MSR_CSTAR        0xC0000083
#define MSR_FMASK        0xC0000084
#define MSR_FSBASE       0xC0000100
#define MSR_GSBASE       0xC0000101
#define MSR_KERNELGSBASE 0xC0000102

static inline u64 msr_read(u32 reg) {
    u64 low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(reg));
    return (high << 32) | low;
}

static inline void msr_write(u32 reg, u64 val) {
    u32 low = val & 0xFFFFFFFF;
    u32 high = (val >> 32) & 0xFFFFFFFF;
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(reg));
}
