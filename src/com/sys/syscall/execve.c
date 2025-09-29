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
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/mmu.h>
#include <lib/spinlock.h>
#include <lib/str.h>
#include <lib/util.h>
#include <stddef.h>
#include <stdint.h>

// TODO: I used to take sched_lock here because back in 0.2.3 not doing it
// somehow crashed the kernel sometimes, but now the opposite seems to happen:
// use icewm, when passing on the main bar, it does execve for some reason and
// has CLOEXEC on a socket, so the socket sybsystem notifies readers with
// com_sys_sched_notify_all which also takes the sched_lock. So I need to check
// which version is correct.

// SYSCALL: execve(const char *path, char *const *argv, char *const *envp)
COM_SYS_SYSCALL(com_sys_syscall_execve) {
    COM_SYS_SYSCALL_UNUSED_START(4);

    arch_context_t *ctx  = COM_SYS_SYSCALL_CONTEXT();
    const char     *path = COM_SYS_SYSCALL_ARG(const char *, 1);
    char *const    *argv = COM_SYS_SYSCALL_ARG(char *const *, 2);
    char *const    *env  = COM_SYS_SYSCALL_ARG(char *const *, 3);

    com_thread_t *thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *proc   = thread->proc;
    kspinlock_acquire(&proc->signal_lock);
    // kspinlock_acquire(&thread->sched_lock);

    com_vmm_context_t *new_vmm_ctx = NULL;
    int                status      = com_sys_elf64_prepare_proc(&new_vmm_ctx,
                                            path,
                                            argv,
                                            env,
                                            proc,
                                            ctx);

    if (0 != status) {
        kspinlock_release(&proc->signal_lock);
        // kspinlock_release(&thread->sched_lock);
        return COM_SYS_SYSCALL_ERR(status);
    }

    com_sys_proc_kill_other_threads(proc, thread);
    com_mm_vmm_switch(new_vmm_ctx);
    com_mm_vmm_destroy_context(proc->vmm_context);
    proc->vmm_context = new_vmm_ctx;

    kspinlock_acquire(&proc->fd_lock);
    for (int i = 0; i < CONFIG_OPEN_MAX; i++) {
        if (NULL != proc->fd[i].file &&
            ((FD_CLOEXEC & proc->fd[i].flags) ||
             (O_CLOEXEC & proc->fd[i].file->flags))) {
            COM_FS_FILE_RELEASE(proc->fd[i].file);
            proc->fd[i] = (com_filedesc_t){0};
        }
    }
    kspinlock_release(&proc->fd_lock);

    for (size_t i = 0; i < NSIG; i++) {
        if (NULL != proc->sigaction[i] &&
            (SIGCHLD != i || SIG_IGN != proc->sigaction[i]->sa_action)) {
            com_mm_slab_free(proc->sigaction[i], sizeof(com_sigaction_t));
            proc->sigaction[i] = NULL;
        }
    }

    ARCH_CONTEXT_INIT_EXTRA(thread->xctx);
    ARCH_CONTEXT_RESTORE_EXTRA(thread->xctx);

    proc->did_execve = true;
    // kspinlock_release(&thread->sched_lock);
    kspinlock_release(&proc->signal_lock);

    return COM_SYS_SYSCALL_OK(0);
}
