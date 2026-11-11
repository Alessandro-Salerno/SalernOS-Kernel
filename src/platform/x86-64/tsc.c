#include <arch/cpu.h>
#include <kernel/com/util.h>
#include <kernel/platform/x86-64/cpuid.h>
#include <kernel/platform/x86-64/pit.h>
#include <kernel/platform/x86-64/tsc.h>
#include <stdatomic.h>

#define TSC_REQUIRED_LEAF       0x80000007
#define TSC_CPUID_AVAIL         (1 << 8)
#define TSC_CALIBRATE_PIT_TICKS (32768)

// From what I could understand, if the TSC is invariant, then it ticks at a
// constant rate across all cores, so we can calibrate it just once Of course
// all of this is terribly stupid because it is not backwards compatible AND
// relies on PIT, which is not precise enough

// NOTE: not static so we can access them from the header which is better for
// inlining
u64 g_tsc_boot_value = 0;
u64 g_tsc_boot_time = 0;

static bool tsc_probe(void) {
    u32 max_leaf = cpuid_ext_max_leaf();
    if (max_leaf < TSC_REQUIRED_LEAF) return false;

    CpuId cpuid;
    cpuid_with_ecx(TSC_REQUIRED_LEAF, 0, &cpuid);
    return cpuid.edx & TSC_CPUID_AVAIL;
}

void tsc_boot(void) { g_tsc_boot_value = tsc_read(); }

void tsc_bsp_init(void) {
    assert(g_tsc_boot_time == 0);
    tsc_init();
    g_tsc_boot_time = tsc_val_to_ns(cpu_get()->tsc_reverse_mult,
                                    g_tsc_boot_value);
}

void tsc_init(void) {
    Cpu *curr_cpu = cpu_get();

    if (!tsc_probe()) {
        // Set some random but realistic frequency for TSC
        u64 tsc_freq = 3 * 1000UL * 1000UL * 1000UL;
        curr_cpu->tsc_reverse_mult = ((__uint128_t)1000000000ULL << 32) /
            tsc_freq;
        return;
    }

    log("initializing tsc");

    if (cpuid_base_max_leaf() >= 0x15) {
        CpuId regs;
        cpuid_with_ecx(0x15, 0, &regs);

        if (regs.eax == 0 || regs.ebx == 0 || regs.ecx == 0) goto calibrate;

        u64 core_freq = regs.ecx;
        u64 core_numerator = regs.ebx;
        u64 core_denominator = regs.eax;
        u64 tsc_freq = core_freq * core_numerator / core_denominator;
        curr_cpu->tsc_reverse_mult = ((__uint128_t)1000000000ULL << 32) /
            tsc_freq;
        return;
    }

calibrate:;
    u64 start_tsc = tsc_read();
    u64 meas_pit = pit_wait_ticks(TSC_CALIBRATE_PIT_TICKS);
    u64 end_tsc = tsc_read();
    u64 tsc_freq = (end_tsc - start_tsc) / meas_pit * PIT_FREQUENCY;
    curr_cpu->tsc_reverse_mult = ((__uint128_t)1000000000ULL << 32) / tsc_freq;
}
