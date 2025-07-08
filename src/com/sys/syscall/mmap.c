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
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/mmu.h>
#include <lib/util.h>
#include <stdint.h>
#include <sys/mman.h>

#define ALLOC_BASE 0x100000000

// ODO: probably have to separate prot and falsg for portability
// SYSCALL: mmap(void *hint, size_t size, uintmax_t flags, int fd, ...)
COM_SYS_SYSCALL(com_sys_syscall_mmap) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();

    uintptr_t hint  = COM_SYS_SYSCALL_ARG(uintptr_t, 1);
    size_t    size  = COM_SYS_SYSCALL_ARG(size_t, 2);
    uintmax_t flags = COM_SYS_SYSCALL_ARG(uintmax_t, 3);
    int       fd    = COM_SYS_SYSCALL_ARG(int, 4);

    if (-1 != fd) {
        return COM_SYS_SYSCALL_ERR(ENOSYS);
    }

    com_proc_t *curr = hdr_arch_cpu_get_thread()->proc;

    flags &= 0xffffffff;
    // KASSERT(MAP_ANONYMOUS & flags);

    size_t    pages = (size + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
    uintptr_t virt;

    if (MAP_FIXED & flags) {
        virt = hint;
    } else {
        com_spinlock_acquire(&curr->pages_lock);
        /*virt = (uintptr_t)curr->used_pages * ARCH_PAGE_SIZE + ALLOC_BASE;
        curr->used_pages += pages;*/
        virt = __atomic_fetch_add(&curr->used_pages, pages, __ATOMIC_SEQ_CST) *
                   ARCH_PAGE_SIZE +
               ALLOC_BASE;
        com_spinlock_release(&curr->pages_lock);
    }

    for (size_t i = 0; i < pages; i++) {
        void *phys  = com_mm_pmm_alloc();
        void *kvirt = (void *)ARCH_PHYS_TO_HHDM(phys);
        arch_mmu_map(curr->page_table,
                     (void *)(virt + i * ARCH_PAGE_SIZE),
                     phys,
                     ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                         ARCH_MMU_FLAGS_USER);
        if (MAP_ANONYMOUS & flags) {
            kmemset(kvirt, ARCH_PAGE_SIZE, 0);
        }
    }

    return COM_SYS_SYSCALL_OK(virt);
}
