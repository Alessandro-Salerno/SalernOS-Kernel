#include <arch/cpu.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>

// SYSCALL: getpid()
SYSCALL(syscall_getppid) {
    SYSCALL_UNUSED_START(0);
    return SYSCALL_OK(cpu_get_thread()->proc->parent_pid);
}
