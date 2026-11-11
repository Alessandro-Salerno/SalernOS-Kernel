#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>

struct sysinfo {
    char cpu[64];
    char gpu[64];
    char kernel[64];
    PMMStats memory;
};

// SYSCALL: sysinfo(struct sysinfo *buf)
SYSCALL(syscall_sysinfo) {
    SYSCALL_UNUSED_CONTEXT();
    SYSCALL_UNUSED_START(2);

    struct sysinfo *sysinfo = SYSCALL_ARG(struct sysinfo *, 1);

    struct sysinfo ksysinfo = {0};
    strcpy(ksysinfo.kernel, "SalernOS Kernel 0.2.5 (indev)");
    pmm_get_stats(&ksysinfo.memory);

    int copy_ret = memcpy_to_user(sysinfo, &ksysinfo, sizeof(ksysinfo));
    if (copy_ret != 0) return SYSCALL_ERR(copy_ret);

    return SYSCALL_OK(0);
}
