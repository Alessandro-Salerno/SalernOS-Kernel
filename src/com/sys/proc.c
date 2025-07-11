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
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/platform/mmu.h>
#include <lib/hashmap.h>
#include <lib/mem.h>
#include <lib/util.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

#define MAX_PROCS 1024

static com_spinlock_t PidLock              = COM_SPINLOCK_NEW();
static com_spinlock_t GlobalProcLock       = COM_SPINLOCK_NEW();
static com_proc_t    *Processes[MAX_PROCS] = {0};
static pid_t          NextPid              = 1;

static khashmap_t     ProcGroupMap;
static com_spinlock_t ProcGroupLock = COM_SPINLOCK_NEW();
static bool           ProcGroupInit = false;

com_proc_t *com_sys_proc_new(arch_mmu_pagetable_t *page_table,
                             pid_t                 parent_pid,
                             com_vnode_t          *root,
                             com_vnode_t          *cwd) {

    com_proc_t *proc   = com_mm_slab_alloc(sizeof(com_proc_t));
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
    proc->num_ref    = 1; // 1 because of the parent
    TAILQ_INIT(&proc->notifications);

    proc->threads_lock = COM_SPINLOCK_NEW();
    TAILQ_INIT(&proc->threads);

    // TODO: use atomic operations (faster)
    com_spinlock_acquire(&PidLock);
    KASSERT(NextPid <= MAX_PROCS);
    proc->pid                = NextPid++;
    Processes[proc->pid - 1] = proc;
    com_spinlock_release(&PidLock);

    proc->fd_lock = COM_SPINLOCK_NEW();
    proc->next_fd = 0;

    proc->pg_lock    = COM_SPINLOCK_NEW();
    proc->proc_group = NULL;

    com_proc_t *parent = com_sys_proc_get_by_pid(parent_pid);
    if (NULL != parent && NULL != parent->proc_group) {
        com_sys_proc_join_group(proc, parent->proc_group);
    }

    return proc;
}

void com_sys_proc_destroy(com_proc_t *proc) {
    com_proc_t *parent = com_sys_proc_get_by_pid(proc->parent_pid);
    if (NULL != parent) {
        __atomic_add_fetch(&proc->num_children, -1, __ATOMIC_SEQ_CST);
    }
    Processes[proc->pid - 1] = NULL;
    com_spinlock_acquire(&proc->pg_lock);
    if (NULL != proc->proc_group) {
        TAILQ_REMOVE(&proc->proc_group->procs, proc, procs);
    }
    com_spinlock_fake_release();
    arch_mmu_pagetable_t *proc_pt = proc->page_table;
    com_mm_slab_free(proc, sizeof(com_proc_t));
    arch_mmu_destroy_table(proc_pt);
}

int com_sys_proc_next_fd(com_proc_t *proc) {
    com_spinlock_acquire(&proc->fd_lock);
    int ret = proc->next_fd;
    proc->next_fd++;
    com_spinlock_release(&proc->fd_lock);
    return ret;
}

com_file_t *com_sys_proc_get_file(com_proc_t *proc, int fd) {
    if (NULL == proc) {
        return NULL;
    }

    if (fd > COM_SYS_PROC_MAX_FDS) {
        return NULL;
    }

    com_spinlock_acquire(&proc->fd_lock);
    com_file_t *file = proc->fd[fd].file;
    com_spinlock_release(&proc->fd_lock);
    COM_FS_FILE_HOLD(file);
    return file;
}

com_proc_t *com_sys_proc_get_by_pid(pid_t pid) {
    if (pid > MAX_PROCS || pid < 1) {
        return NULL;
    }

    return Processes[pid - 1];
}

void com_sys_proc_acquire_glock(void) {
    com_spinlock_acquire(&GlobalProcLock);
}

void com_sys_proc_release_glock(void) {
    com_spinlock_release(&GlobalProcLock);
}

void com_sys_proc_wait(com_proc_t *proc) {
    com_sys_sched_wait(&proc->notifications, &GlobalProcLock);
}

void com_sys_proc_add_thread(com_proc_t *proc, com_thread_t *thread) {
    KASSERT(!proc->exited);
    com_spinlock_acquire(&proc->threads_lock);
    TAILQ_INSERT_TAIL(&proc->threads, thread, proc_threads);
    __atomic_add_fetch(&proc->num_ref, 1, __ATOMIC_SEQ_CST);
    com_spinlock_release(&proc->threads_lock);
}
void com_sys_proc_remove_thread(com_proc_t *proc, com_thread_t *thread) {
    com_spinlock_acquire(&proc->threads_lock);
    com_sys_proc_remove_thread_nolock(proc, thread);
    com_spinlock_release(&proc->threads_lock);
}

void com_sys_proc_remove_thread_nolock(com_proc_t        *proc,
                                       struct com_thread *thread) {
    TAILQ_REMOVE(&proc->threads, thread, proc_threads);
}

void com_sys_proc_exit(com_proc_t *proc, int status) {
    com_sys_proc_acquire_glock();

    com_spinlock_acquire(&proc->fd_lock);
    for (int i = 0; i < proc->next_fd; i++) {
        if (NULL != proc->fd[i].file) {
            COM_FS_FILE_RELEASE(proc->fd[i].file);
        }
    }
    com_spinlock_release(&proc->fd_lock);

    com_proc_t *parent = com_sys_proc_get_by_pid(proc->parent_pid);
    proc->exited       = true;
    proc->exit_status  = status;

    if (NULL != parent) {
        com_sys_sched_notify(&parent->notifications);
    }

    com_sys_proc_release_glock();
}

com_proc_group_t *com_sys_proc_new_group(com_proc_t         *leader,
                                         com_proc_session_t *session) {
    com_spinlock_acquire(&ProcGroupLock);
    if (!ProcGroupInit) {
        KHASHMAP_INIT(&ProcGroupMap);
        ProcGroupInit = true;
    }

    com_proc_group_t *group = com_mm_slab_alloc(sizeof(com_proc_group_t));
    group->procs_lock       = COM_SPINLOCK_NEW();
    group->pgid             = leader->pid;
    group->session          = session;
    TAILQ_INIT(&group->procs);

    if (NULL == session && NULL != leader->proc_group) {
        group->session = leader->proc_group->session;
    }

    KASSERT(0 == KHASHMAP_PUT(&ProcGroupMap, &group->pgid, group));
    KDEBUG("created pgid=%d", group->pgid);
    com_spinlock_release(&ProcGroupLock);
    return group;
}

int com_sys_proc_join_group(com_proc_t *proc, com_proc_group_t *group) {
    com_spinlock_acquire(&proc->pg_lock);
    int ret = com_sys_proc_join_group_nolock(proc, group);
    com_spinlock_release(&proc->pg_lock);
    return ret;
}

int com_sys_proc_join_group_nolock(com_proc_t *proc, com_proc_group_t *group) {
    if (NULL != proc->proc_group) {
        KASSERT(NULL != proc->proc_group->session);
        KASSERT(NULL != group->session);

        // If sid is equal to the pid, then this process is the session leader
        // (useful, for example, in setsid). Whereas, if the two are different,
        // then it means that the process is trying to migrate sessions (which
        // POSIX does not allow)
        /*if (proc->proc_group->session->sid != proc->pid &&
            proc->proc_group->session->sid != group->session->sid) {
            return EPERM;
        }*/

        com_spinlock_acquire(&proc->proc_group->procs_lock);
        TAILQ_REMOVE(&proc->proc_group->procs, proc, procs);
        com_spinlock_release(&proc->proc_group->procs_lock);
    }

    com_spinlock_acquire(&group->procs_lock);
    TAILQ_INSERT_TAIL(&group->procs, proc, procs);
    proc->proc_group = group;
    KDEBUG("pid=%d is now part of group pgid=%d", proc->pid, group->pgid);
    com_spinlock_release(&group->procs_lock);
    return 0;
}

com_proc_group_t *com_sys_proc_get_group_by_pgid(pid_t pgid) {
    com_spinlock_acquire(&ProcGroupLock);
    com_proc_group_t *group;
    if (0 != KHASHMAP_GET(&group, &ProcGroupMap, &pgid)) {
        group = NULL;
    }
    com_spinlock_release(&ProcGroupLock);
    return group;
}

com_proc_session_t *com_sys_proc_new_session_nolock(com_proc_t  *leader,
                                                    com_vnode_t *tty) {
    com_proc_session_t *session = com_mm_slab_alloc(sizeof(com_proc_session_t));
    if (NULL != tty) {
        COM_FS_VFS_VNODE_HOLD(tty);
    }
    session->tty     = tty;
    session->pg_lock = COM_SPINLOCK_NEW();
    session->sid     = leader->pid;
    TAILQ_INIT(&session->proc_groups);

    if (NULL != leader->proc_group) {
        com_spinlock_acquire(&leader->proc_group->procs_lock);
        TAILQ_REMOVE(&leader->proc_group->procs, leader, procs);
        com_spinlock_release(&leader->proc_group->procs_lock);
    }

    com_proc_group_t *new_group = com_sys_proc_new_group(leader, session);
    com_sys_proc_join_group_nolock(leader, new_group);

    KDEBUG("pid=%d is now leader of session sid=%d", leader->pid, session->sid);
    return session;
}
