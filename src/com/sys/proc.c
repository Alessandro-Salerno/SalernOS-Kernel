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

#include <arch/info.h>
#include <arch/mmu.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/platform/mmu.h>
#include <lib/mem.h>
#include <lib/util.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

static com_spinlock_t PidLock        = COM_SPINLOCK_NEW();
static com_spinlock_t GlobalProcLock = COM_SPINLOCK_NEW();
static com_proc_t    *Processes[256] = {0};
static uintmax_t      NextPid        = 1;

com_proc_t *com_sys_proc_new(arch_mmu_pagetable_t *page_table,
                             uintmax_t             parent_pid,
                             com_vnode_t          *root,
                             com_vnode_t          *cwd) {
    com_proc_t *proc = (com_proc_t *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
    kmemset(proc, ARCH_PAGE_SIZE, 0);
    proc->page_table   = page_table;
    proc->exited       = false;
    proc->exit_status  = 0;
    proc->num_children = 0;
    proc->parent_pid   = parent_pid;
    COM_FS_VFS_VNODE_HOLD(root);
    COM_FS_VFS_VNODE_HOLD(cwd);
    proc->root       = root;
    proc->cwd        = cwd;
    proc->pages_lock = COM_SPINLOCK_NEW();
    proc->used_pages = 0;
    TAILQ_INIT(&proc->waiting_on);

    // TODO: use atomic operations (faster)
    hdr_com_spinlock_acquire(&PidLock);
    KASSERT(NextPid <= 256);
    proc->pid                = NextPid++;
    Processes[proc->pid - 1] = proc;
    hdr_com_spinlock_release(&PidLock);

    proc->fd_lock = COM_SPINLOCK_NEW();
    proc->next_fd = 0;

    return proc;
}

void com_sys_proc_destroy(com_proc_t *proc) {
    com_proc_t *parent = com_sys_proc_get_by_pid(proc->parent_pid);
    if (NULL != parent) {
        parent->num_children--;
    }
    Processes[proc->pid - 1] = NULL;
    com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(proc));
}

uintmax_t com_sys_proc_next_fd(com_proc_t *proc) {
    hdr_com_spinlock_acquire(&proc->fd_lock);
    uintmax_t ret = proc->next_fd;
    proc->next_fd++;
    hdr_com_spinlock_release(&proc->fd_lock);
    return ret;
}

com_file_t *com_sys_proc_get_file(com_proc_t *proc, uintmax_t fd) {
    if (NULL == proc) {
        return NULL;
    }

    if (fd > 15) {
        return NULL;
    }

    hdr_com_spinlock_acquire(&proc->fd_lock);
    com_file_t *file = proc->fd[fd].file;
    hdr_com_spinlock_release(&proc->fd_lock);
    COM_FS_FILE_HOLD(file);
    return file;
}

com_proc_t *com_sys_proc_get_by_pid(uintmax_t pid) {
    if (pid > 256 || 0 == pid) {
        return NULL;
    }

    return Processes[pid - 1];
}

void com_sys_proc_acquire_glock(void) {
    hdr_com_spinlock_acquire(&GlobalProcLock);
}

void com_sys_proc_release_glock(void) {
    hdr_com_spinlock_release(&GlobalProcLock);
}

void com_sys_proc_wait(com_proc_t *proc) {
    com_sys_sched_wait(&proc->waiting_on, &GlobalProcLock);
}

void com_sys_proc_exit(com_proc_t *proc, int status) {
    com_sys_proc_acquire_glock();

    com_proc_t *parent = com_sys_proc_get_by_pid(proc->parent_pid);
    proc->exited       = true;
    proc->exit_status  = status;

    if (NULL != parent) {
        com_sys_sched_notify(&parent->waiting_on);
    }

    com_sys_proc_release_glock();

    hdr_com_spinlock_acquire(&proc->fd_lock);
    for (size_t i = 0; i < proc->next_fd; i++) {
        if (NULL != proc->fd[i].file) {
            COM_FS_FILE_RELEASE(proc->fd[i].file);
        }
    }
    hdr_com_spinlock_release(&proc->fd_lock);
}
