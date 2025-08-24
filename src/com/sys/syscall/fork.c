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

#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/context.h>
#include <kernel/platform/mmu.h>
#include <lib/str.h>
#include <lib/util.h>
#include <stddef.h>
#include <stdint.h>

COM_SYS_SYSCALL(com_sys_syscall_fork) {
    COM_SYS_SYSCALL_UNUSED_START(1);

    arch_context_t *ctx = COM_SYS_SYSCALL_CONTEXT();

    com_thread_t *cur_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *proc       = cur_thread->proc;

    com_spinlock_acquire(&proc->fd_lock);
    com_spinlock_acquire(&proc->pages_lock);

    arch_mmu_pagetable_t *new_pt = arch_mmu_duplicate_table(proc->page_table);

    com_proc_t *new_proc =
        com_sys_proc_new(new_pt, proc->pid, proc->root, proc->cwd);
    // NOTE: process group is inherited from parent in proc_new
    com_thread_t *new_thread = com_sys_thread_new(new_proc, NULL, 0, 0);
    ARCH_CONTEXT_FORK_EXTRA(new_thread->xctx, cur_thread->xctx);

    if (NULL == new_pt || NULL == new_proc || NULL == new_thread) {
        return COM_SYS_SYSCALL_ERR(ENOMEM);
    }

    for (int i = 0; i < CONFIG_OPEN_MAX; i++) {
        if (NULL != proc->fd[i].file) {
            new_proc->fd[i].flags = proc->fd[i].flags;
            COM_FS_FILE_HOLD(proc->fd[i].file);
            new_proc->fd[i].file = proc->fd[i].file;
        }
    }

    new_proc->next_fd    = proc->next_fd;
    new_proc->used_pages = proc->used_pages;

    ARCH_CONTEXT_FORK(new_thread, *ctx);

    __atomic_add_fetch(&proc->num_children, 1, __ATOMIC_SEQ_CST);
    com_spinlock_release(&proc->fd_lock);
    com_spinlock_release(&proc->pages_lock);

    com_spinlock_acquire(&proc->signal_lock);
    for (size_t i = 0; i < NSIG; i++) {
        if (NULL != proc->sigaction[i]) {
            com_sigaction_t *sa    = com_mm_slab_alloc(sizeof(com_sigaction_t));
            *sa                    = *proc->sigaction[i];
            new_proc->sigaction[i] = sa;
        }
    }
    com_spinlock_release(&proc->signal_lock);

    new_thread->runnable = false;
    com_sys_thread_ready(new_thread);

    return COM_SYS_SYSCALL_OK(new_proc->pid);
}
