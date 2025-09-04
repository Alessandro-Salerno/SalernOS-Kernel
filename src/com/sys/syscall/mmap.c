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
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/mmu.h>
#include <lib/spinlock.h>
#include <lib/util.h>
#include <stdint.h>
#include <sys/mman.h>

static com_syscall_ret_t file_mmap(com_proc_t *curr_proc,
                                   int         fd,
                                   size_t      size,
                                   uintmax_t   flags,
                                   uintmax_t   prot,
                                   uintmax_t   off,
                                   uintptr_t   hint) {
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
                                  flags,
                                  prot,
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

// TODO: properly handle flags, prot, off
// SYSCALL: mmap(void *hint, size_t size, uintmax_t flags, int fd, ...)
COM_SYS_SYSCALL(com_sys_syscall_mmap) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();

    uintptr_t hint  = COM_SYS_SYSCALL_ARG(uintptr_t, 1);
    size_t    size  = COM_SYS_SYSCALL_ARG(size_t, 2);
    uintmax_t flags = COM_SYS_SYSCALL_ARG(uintmax_t, 3);
    int       fd    = COM_SYS_SYSCALL_ARG(int, 4);

    flags &= 0xffffffff;
    com_proc_t *curr_proc = ARCH_CPU_GET_THREAD()->proc;

    if (-1 != fd) {
        return file_mmap(curr_proc,
                         fd,
                         size,
                         flags,
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         0,
                         hint);
    }

    size_t    pages = (size + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
    uintptr_t virt;

    if (MAP_FIXED & flags) {
        virt = hint;
    } else {
        kspinlock_acquire(&curr_proc->pages_lock);
        virt = curr_proc->used_pages * ARCH_PAGE_SIZE + CONFIG_VMM_ANON_START;
        curr_proc->used_pages += pages;
        kspinlock_release(&curr_proc->pages_lock);
    }

    for (size_t i = 0; i < pages; i++) {
        void *phys = com_mm_pmm_alloc();
        arch_mmu_map(curr_proc->page_table,
                     (void *)(virt + i * ARCH_PAGE_SIZE),
                     phys,
                     ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                         ARCH_MMU_FLAGS_USER);
    }

#if CONFIG_PMM_ZERO == CONST_PMM_ZERO_OFF
    if (MAP_ANONYMOUS & flags) {
        kmemset((void *)virt, ARCH_PAGE_SIZE * pages, 0);
    }
#endif

    return COM_SYS_SYSCALL_OK(virt);
}
