/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#include <arch/cpu.h>
#include <arch/info.h>
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/mmu.h>
#include <lib/spinlock.h>
#include <lib/util.h>
#include <stdint.h>
#include <sys/mman.h>

static com_syscall_ret_t file_mmap(com_proc_t      *curr_proc,
                                   int              fd,
                                   size_t           size,
                                   int              vmm_flags,
                                   arch_mmu_flags_t mmu_flags,
                                   uintmax_t        off,
                                   uintptr_t        hint) {
    com_syscall_ret_t ret  = COM_SYS_SYSCALL_BASE_OK();
    com_file_t       *file = com_sys_proc_get_file(curr_proc, fd);
    if (NULL == file) {
        ret = COM_SYS_SYSCALL_ERR(EBADF);
        goto end;
    }

    void *mmap_ret;
    int   vfs_err = com_fs_vfs_mmap(&mmap_ret,
                                  file->vnode,
                                  hint,
                                  size,
                                  vmm_flags,
                                  mmu_flags,
                                  off);
    if (0 != vfs_err) {
        ret = COM_SYS_SYSCALL_ERR(vfs_err);
        goto end;
    }

    ret = COM_SYS_SYSCALL_OK(mmap_ret);

end:
    COM_FS_FILE_RELEASE(file);
    return ret;
}

static inline int make_vmm_flags(int flags, int prot) {
    (void)prot;
    int vmm_flags = 0;

    if (!(MAP_FIXED & flags)) {
        vmm_flags |= COM_MM_VMM_FLAGS_NOHINT;
    }

    if ((MAP_ANONYMOUS | MAP_FILE) & flags) {
        vmm_flags |= COM_MM_VMM_FLAGS_ANONYMOUS;
    }

    return vmm_flags;
}

// TODO: properly handle flags, prot, off
static inline arch_mmu_flags_t make_mmu_flags(int flags, int prot) {
    (void)flags;
    (void)prot;
    return ARCH_MMU_FLAGS_USER | ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE;
}

// SYSCALL: mmap(void *hint, size_t size, uintmax_t flags, int fd, ...)
COM_SYS_SYSCALL(com_sys_syscall_mmap) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();

    uintptr_t hint  = COM_SYS_SYSCALL_ARG(uintptr_t, 1);
    size_t    size  = COM_SYS_SYSCALL_ARG(size_t, 2);
    uintmax_t flags = COM_SYS_SYSCALL_ARG(uintmax_t, 3);
    int       fd    = COM_SYS_SYSCALL_ARG(int, 4);

    int prot = (flags >> 32) & 0xffffffff;
    flags &= 0xffffffff;

    com_proc_t *curr_proc = ARCH_CPU_GET_THREAD()->proc;

    int              vmm_flags = make_vmm_flags(flags, prot);
    arch_mmu_flags_t mmu_flags = make_mmu_flags(flags, prot);

    if (-1 != fd) {
        vmm_flags |= COM_MM_VMM_FLAGS_ANONYMOUS;
        return file_mmap(curr_proc, fd, size, vmm_flags, mmu_flags, 0, hint);
    }

    // Allocate anonymous pages
    void *virt = com_mm_vmm_map(curr_proc->vmm_context,
                                (void *)hint,
                                NULL,
                                size,
                                vmm_flags | COM_MM_VMM_FLAGS_ALLOCATE,
                                mmu_flags);

    return COM_SYS_SYSCALL_OK(virt);
}
