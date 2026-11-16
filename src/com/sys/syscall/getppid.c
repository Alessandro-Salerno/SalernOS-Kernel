#include <arch/cpu.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>

// SYSCALL: getpid()
SYSCALL(syscall_getppid) {
    SYSCALL_UNUSED_START(0);
    Proc *curr_proc = cpu_get_thread()->proc;
    pid_t parent_pid = __atomic_load_n(&curr_proc->parent_pid, __ATOMIC_RELAXED);
    return SYSCALL_OK(parent_pid);
}
