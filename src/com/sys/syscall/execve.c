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
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/mmu.h>
#include <lib/str.h>
#include <stddef.h>
#include <stdint.h>

// TODO: when processes will have more than one thread, there could be a
// race condition where two threads try to run exec() and all of this
// explodes. There should be a sort of "exec" lock. Also, if there are multiple
// threads, what happens to the other threads? SYSCALL: execve(const char *path,
// char *const *argv, char *const *envp)
COM_SYS_SYSCALL(com_sys_syscall_execve) {
    COM_SYS_SYSCALL_UNUSED_START(4);

    arch_context_t *ctx  = COM_SYS_SYSCALL_CONTEXT();
    const char     *path = COM_SYS_SYSCALL_ARG(const char *, 1);
    char *const    *argv = COM_SYS_SYSCALL_ARG(char *const *, 2);
    char *const    *env  = COM_SYS_SYSCALL_ARG(char *const *, 3);

    com_thread_t         *thread = hdr_arch_cpu_get_thread();
    com_proc_t           *proc   = thread->proc;
    arch_mmu_pagetable_t *new_pt = NULL;
    int                   status =
        com_sys_elf64_prepare_proc(&new_pt, path, argv, env, proc, ctx);

    if (0 != status) {
        return COM_SYS_SYSCALL_ERR(status);
    }

    arch_mmu_switch(new_pt);
    arch_mmu_destroy_table(proc->page_table);
    proc->page_table = new_pt;

    for (int i = 0; i < proc->next_fd; i++) {
        if (NULL != proc->fd[i].file && (FD_CLOEXEC & proc->fd[i].flags)) {
            COM_FS_FILE_RELEASE(proc->fd[i].file);
        }
    }

    ARCH_CONTEXT_INIT_EXTRA(thread->xctx);
    ARCH_CONTEXT_RESTORE_EXTRA(thread->xctx);
    return COM_SYS_SYSCALL_OK(0);
}
