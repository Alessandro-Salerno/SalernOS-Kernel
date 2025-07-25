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
#include <lib/util.h>
#include <stddef.h>
#include <stdint.h>

// SYSCALL: execve(const char *path, char *const *argv, char *const *envp)
COM_SYS_SYSCALL(com_sys_syscall_execve) {
    COM_SYS_SYSCALL_UNUSED_START(4);

    arch_context_t *ctx  = COM_SYS_SYSCALL_CONTEXT();
    const char     *path = COM_SYS_SYSCALL_ARG(const char *, 1);
    char *const    *argv = COM_SYS_SYSCALL_ARG(char *const *, 2);
    char *const    *env  = COM_SYS_SYSCALL_ARG(char *const *, 3);

    com_thread_t *thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *proc   = thread->proc;
    com_spinlock_acquire(&proc->signal_lock);
    com_spinlock_acquire(&thread->sched_lock);

    arch_mmu_pagetable_t *new_pt = NULL;
    int                   status =
        com_sys_elf64_prepare_proc(&new_pt, path, argv, env, proc, ctx);

    if (0 != status) {
        com_spinlock_release(&proc->signal_lock);
        com_spinlock_release(&thread->sched_lock);
        return COM_SYS_SYSCALL_ERR(status);
    }

    // TODO: this is wrong for multithreaded applications but it's agood start
    com_spinlock_acquire(&proc->threads_lock);
    com_thread_t *t, *_;
    TAILQ_FOREACH_SAFE(t, &proc->threads, proc_threads, _) {
        if (t != thread) {
            com_spinlock_acquire(&t->sched_lock);
            t->exited   = true;
            t->runnable = false;
            com_sys_proc_remove_thread_nolock(proc, t);
            com_spinlock_release(&t->sched_lock);
        }
    }
    com_spinlock_release(&proc->threads_lock);

    arch_mmu_switch(new_pt);
    arch_mmu_destroy_table(proc->page_table);
    proc->page_table = new_pt;

    for (int i = 0; i < proc->next_fd; i++) {
        if (NULL != proc->fd[i].file && (FD_CLOEXEC & proc->fd[i].flags)) {
            COM_FS_FILE_RELEASE(proc->fd[i].file);
            KDEBUG("(pid=%d) closed fd %d because it has FD_CLOEXECL, ref=%d",
                   proc->pid,
                   i,
                   (proc->fd[i].file) ? proc->fd[i].file->num_ref : 0);
            proc->fd[i] = (com_filedesc_t){0};
        }
    }

    for (size_t i = 0; i < NSIG; i++) {
        if (NULL != proc->sigaction[i] &&
            (SIGCHLD != i || SIG_IGN != proc->sigaction[i]->sa_action)) {
            com_mm_slab_free(proc->sigaction[i], sizeof(com_sigaction_t));
            proc->sigaction[i] = NULL;
        }
    }

    ARCH_CONTEXT_INIT_EXTRA(thread->xctx);
    ARCH_CONTEXT_RESTORE_EXTRA(thread->xctx);

    com_spinlock_release(&thread->sched_lock);
    com_spinlock_release(&proc->signal_lock);

    return COM_SYS_SYSCALL_OK(0);
}
