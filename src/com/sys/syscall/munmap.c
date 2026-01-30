/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
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

COM_SYS_SYSCALL(com_sys_syscall_munmap) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    void  *addr = COM_SYS_SYSCALL_ARG(void *, 1);
    size_t len  = COM_SYS_SYSCALL_ARG(size_t, 2);

    if (0 != (uintptr_t)addr % ARCH_PAGE_SIZE ||
        addr > ARCH_MMU_KERNELSPACE_START || 0 == len ||
        addr + len > ARCH_MMU_KERNELSPACE_START) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_mm_vmm_unmap(curr_proc->vmm_context, addr, len);
    return COM_SYS_SYSCALL_OK(0);
}
