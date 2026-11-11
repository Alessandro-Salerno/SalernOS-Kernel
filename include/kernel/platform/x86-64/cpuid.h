#pragma once

#include <kernel/com/util.h>

#define CPUID_LEAF_1_EDX_TSC (1 << 4)
#define CPUID_LEAF_1_EDX_HTT (1 << 28)

typedef struct CpuId {
    u32 eax;
    u32 ebx;
    u32 ecx;
    u32 edx;
} CpuId;

// TAKEn: Mathewnd/Astral
static inline void cpuid_with_ecx(u32 leaf, u32 ecx, CpuId *out_cpuid) {
    asm volatile("cpuid"
                 : "=a"(out_cpuid->eax), "=b"(out_cpuid->ebx),
                   "=c"(out_cpuid->ecx), "=d"(out_cpuid->edx)
                 : "a"(leaf), "c"(ecx));
}

static inline u32 cpuid_ext_max_leaf(void) {
    CpuId regs;
    cpuid_with_ecx(0x80000000, 0, &regs);
    return regs.eax;
}

static inline u32 cpuid_base_max_leaf(void) {
    CpuId regs;
    cpuid_with_ecx(0, 0, &regs);
    return regs.eax;
}
