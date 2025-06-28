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

// TODO: change this, just like stack and other offsets
#define ALLOC_BASE 0x100000000

com_syscall_ret_t com_sys_syscall_mmap(arch_context_t *ctx,
                                       uintmax_t       hint,
                                       uintmax_t       size,
                                       uintmax_t       flags,
                                       uintmax_t       ufd) {
    (void)ctx;

    int fd = (int)ufd;

    if (-1 != fd) {
        return (com_syscall_ret_t){.err = ENOSYS};
    }

    com_proc_t *curr = hdr_arch_cpu_get_thread()->proc;

    flags &= 0xffffffff;
    // KASSERT(MAP_ANONYMOUS & flags);

    size_t    pages = (size + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;
    uintptr_t virt;

    if (MAP_FIXED & flags) {
        virt = hint;
    } else {
        hdr_com_spinlock_acquire(&curr->pages_lock);
        virt = (uintptr_t)curr->used_pages * ARCH_PAGE_SIZE + ALLOC_BASE;
        curr->used_pages += pages;
        hdr_com_spinlock_release(&curr->pages_lock);
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

    return (com_syscall_ret_t){.value = virt, .err = 0};
}
