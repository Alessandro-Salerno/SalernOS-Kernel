#include <arch/cpu.h>
#include <arch/info.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <kernel/platform/mmu.h>
#include <sys/mman.h>

SYSCALL(syscall_munmap) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(3);

    void *addr = SYSCALL_ARG(void *, 1);
    usize len = SYSCALL_ARG(usize, 2);

    if ((uptr)addr % PAGE_SIZE != 0 || len == 0 ||
        !MMU_RANGE_IS_USER(addr, len)) {
        return SYSCALL_ERR(EINVAL);
    }

    Thread *curr_thread = cpu_get_thread();
    Proc *curr_proc = curr_thread->proc;

    vmm_unmap(curr_proc->vmm_context, addr, len);
    return SYSCALL_OK(0);
}
