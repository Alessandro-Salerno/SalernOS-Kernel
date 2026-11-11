#include <arch/cpu.h>
#include <arch/mmu.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>

// SYSCALL: exit(int status)
SYSCALL(syscall_exit) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(2);

    int exit_status = SYSCALL_ARG(int, 1);

    proc_terminate(exit_status, SIGNAL_NONE);
    __builtin_unreachable();
}
