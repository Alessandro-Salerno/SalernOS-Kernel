/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/util.h>
#include <stdint.h>
#include <sys/mman.h>

#include "arch/mmu.h"
#include "com/mm/pmm.h"
#include "platform/mmu.h"

#define ALLOC_BASE 1

com_syscall_ret_t com_sys_syscall_mmap(arch_context_t *ctx,
                                       uintmax_t       hint,
                                       uintmax_t       size,
                                       uintmax_t       flags,
                                       uintmax_t       unused) {
  (void)ctx;
  (void)unused;
  (void)hint; // TODO: use hint

  com_proc_t *curr = hdr_arch_cpu_get_thread()->proc;

  flags &= 0xffffffff;
  // TODO: support other flags
  KASSERT(MAP_ANONYMOUS & flags);

  size_t pages = (size + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;

  hdr_com_spinlock_acquire(&curr->pages_lock);
  uintptr_t virt = (uintptr_t)curr->used_pages * ARCH_PAGE_SIZE + ALLOC_BASE;
  curr->used_pages += pages;
  hdr_com_spinlock_release(&curr->pages_lock);

  for (size_t i = 0; i < pages; i++) {
    arch_mmu_map(curr->page_table,
                 (void *)(virt + i * ARCH_PAGE_SIZE),
                 com_mm_pmm_alloc(),
                 ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                     ARCH_MMU_FLAGS_USER);
  }

  return (com_syscall_ret_t){.value = virt, .err = 0};
}
