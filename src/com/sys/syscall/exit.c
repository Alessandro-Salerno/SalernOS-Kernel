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
#include <arch/mmu.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/mmu.h>
#include <stdint.h>

// TODO: what happens if two threads of the same process call exit?
com_syscall_ret_t com_sys_syscall_exit(arch_context_t *ctx,
                                       uintmax_t       status,
                                       uintmax_t       unused1,
                                       uintmax_t       unused2,
                                       uintmax_t       unused3) {
    (void)ctx;
    (void)unused1;
    (void)unused2;
    (void)unused3;

    com_thread_t *curr_thread = hdr_arch_cpu_get_thread();
    com_proc_t   *curr_proc   = curr_thread->proc;

    com_sys_proc_exit(curr_proc, (int)status);
    hdr_com_spinlock_acquire(&hdr_arch_cpu_get()->runqueue_lock);
    curr_thread->runnable = false;
    hdr_com_spinlock_release(&hdr_arch_cpu_get()->runqueue_lock);
    com_sys_sched_yield();

    __builtin_unreachable();
}
