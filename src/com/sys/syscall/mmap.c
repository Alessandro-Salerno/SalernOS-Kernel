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

static SyscallRet file_mmap(Proc *curr_proc, int fd, usize size, int vmm_flags,
                            MMUFlags mmu_flags, u64 off, void *hint) {
    SyscallRet ret = SYSCALL_BASE_OK();
    File *file = proc_get_file(curr_proc, fd);
    if (file == NULL) {
        ret = SYSCALL_ERR(EBADF);
        goto end;
    }

    void *mmap_ret;
    int vfs_err = vfs_mmap(file->vnode, NULL, hint, size,
                           vmm_flags | VMM_FLAGS_FILE, mmu_flags, off,
                           &mmap_ret);
    if (vfs_err != 0) {
        ret = SYSCALL_ERR(vfs_err);
        goto end;
    }

    ret = SYSCALL_OK(mmap_ret);

end:
    FILE_REL(file);
    return ret;
}

static inline int make_vmm_flags(int flags, int prot) {
    (void)prot;
    int vmm_flags = 0;

    if (!(flags & MAP_FIXED)) vmm_flags |= VMM_FLAGS_NOHINT;
    if (flags & MAP_ANONYMOUS) vmm_flags |= VMM_FLAGS_ANONYMOUS;
    if (flags & MAP_SHARED) vmm_flags |= VMM_FLAGS_SHARED;
    else if (flags & MAP_PRIVATE) vmm_flags |= VMM_FLAGS_PRIVATE;

    return vmm_flags;
}

static inline MMUFlags make_mmu_flags(int flags, int prot) {
    (void)flags;

    MMUFlags mmu_prot = MMU_FLAGS_PRESENT | MMU_FLAGS_USER;
    if (prot & PROT_WRITE) mmu_prot |= MMU_FLAGS_WRITE;
    if (prot & PROT_READ) mmu_prot |= MMU_FLAGS_READ;
    /*if (!(PROT_EXEC & prot)) {
        mmu_prot |= MMU_FLAGS_NOEXEC;
    }*/

    return mmu_prot;
}

// SYSCALL: mmap(void *hint, usize size, int flags, int prot, int fd, off_t
// off)
SYSCALL(syscall_mmap) {
    SYSCALL_UNUSED_CONTEXT();

    void *hint = SYSCALL_ARG(void *, 1);
    usize size = SYSCALL_ARG(usize, 2);
    int flags = SYSCALL_ARG(int, 3);
    int prot = SYSCALL_ARG(int, 4);
    int fd = SYSCALL_ARG(int, 5);
    off_t off = SYSCALL_ARG(off_t, 6);

    if (size == 0 || (fd != -1 && (off < 0 || off % PAGE_SIZE != 0))) {
        return SYSCALL_ERR(EINVAL);
    }

    if ((flags & MAP_FIXED) && !MMU_RANGE_IS_USER(hint, size)) {
        return SYSCALL_ERR(ENOMEM);
    }

    Proc *curr_proc = cpu_get_thread()->proc;

    int vmm_flags = make_vmm_flags(flags, prot);
    MMUFlags mmu_flags = make_mmu_flags(flags, prot);

    if (fd != -1) {
        vmm_flags |= VMM_FLAGS_ANONYMOUS;
        return file_mmap(curr_proc, fd, size, vmm_flags, mmu_flags, off, hint);
    }

    // Allocate anonymous pages
    void *virt = vmm_map(curr_proc->vmm_context, hint, NULL, size,
                         vmm_flags | VMM_FLAGS_ALLOCATE, mmu_flags);

    if (virt == NULL) return SYSCALL_ERR(ENOMEM);

    return SYSCALL_OK(virt);
}
