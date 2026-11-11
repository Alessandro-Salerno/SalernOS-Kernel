#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <time.h>

// SYSCALL: clock_get(int clock, struct timespec *ts)
SYSCALL(syscall_clock_get) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    int clock = SYSCALL_ARG(int, 1);
    struct timespec *ts = SYSCALL_ARG(void *, 2);

    (void)clock; // TODO: what is this?
    u64 ns = cpu_get_time();
    struct timespec kts = {.tv_sec = ns / NANOS_PER_SEC,
                           .tv_nsec = ns % NANOS_PER_SEC};

    int copy_ret = memcpy_to_user(ts, &kts, sizeof(kts));
    if (copy_ret != 0) return SYSCALL_ERR(copy_ret);

    return SYSCALL_OK(0);
}
