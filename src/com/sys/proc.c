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

#include <arch/info.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/platform/mmu.h>
#include <lib/hashmap.h>
#include <lib/mem.h>
#include <lib/radixtree.h>
#include <lib/util.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <vendor/tailq.h>

static kspinlock_t  PIDNamespaceLock = KSPINLOCK_NEW();
static kradixtree_t Processes;
static pid_t        NextPid = 1;
static kradixtree_t ProcGroupMap;

static void proc_update_fd_hint(com_proc_t *proc) {
    // Best case: there's a file after the hint
    for (; proc->next_fd < CONFIG_OPEN_MAX; proc->next_fd++) {
        if (NULL == proc->fd[proc->next_fd].file) {
            return;
        }
    }

    // Worst case: retry from the start, files might have been liberated without
    // calling close
    for (proc->next_fd = 0; proc->next_fd < CONFIG_OPEN_MAX &&
                            NULL != proc->fd[proc->next_fd].file;
         proc->next_fd++);
}

com_proc_t *com_sys_proc_new(com_vmm_context_t *vmm_context,
                             pid_t              parent_pid,
                             com_vnode_t       *root,
                             com_vnode_t       *cwd) {
    com_proc_t *proc    = com_mm_slab_alloc(sizeof(com_proc_t));
    proc->vmm_context   = vmm_context;
    proc->exited        = false;
    proc->stop_signal   = COM_IPC_SIGNAL_NONE;
    proc->stop_notified = false;
    proc->exit_status   = 0;
    proc->num_children  = 0;
    proc->parent_pid    = parent_pid;
    COM_FS_VFS_VNODE_HOLD(root);
    COM_FS_VFS_VNODE_HOLD(cwd);
    proc->root                = root;
    proc->cwd                 = cwd;
    proc->num_ref             = 1; // 1 because of the parent
    proc->num_running_threads = 0;
    KSYNC_INIT_SPINLOCK(&proc->waitpid_condvar);
    COM_SYS_THREAD_WAITLIST_INIT(&proc->waitpid_waitlist);

    proc->threads_lock = KSPINLOCK_NEW();
    TAILQ_INIT(&proc->threads);

    kspinlock_acquire(&Processes.lock);
    KASSERT(NextPid <= CONFIG_PROC_MAX);
    proc->pid = NextPid++;
    kradixtree_put_nolock(&Processes, proc->pid - 1, proc);
    kspinlock_release(&Processes.lock);

    proc->fd_lock = KSPINLOCK_NEW();
    proc->next_fd = 0;

    proc->pg_lock    = KSPINLOCK_NEW();
    proc->proc_group = NULL;
    proc->did_execve = false;

    proc->signal_lock = KSPINLOCK_NEW();
    COM_IPC_SIGNAL_SIGMASK_INIT(&proc->pending_signals);

    com_proc_t *parent = com_sys_proc_get_by_pid(parent_pid);
    if (NULL != parent) {
        kspinlock_acquire(&parent->pg_lock);
        if (NULL != parent->proc_group) {
            com_sys_proc_join_group(proc, parent->proc_group);
        }
        kspinlock_release(&parent->pg_lock);
        COM_SYS_PROC_RELEASE(parent);
    }

    return proc;
}

void com_sys_proc_destroy(com_proc_t *proc) {
    KASSERT(0 == proc->num_ref);
    KASSERT(proc->exited);

    com_vmm_context_t *proc_vmm_ctx = proc->vmm_context;
    com_mm_slab_free(proc, sizeof(com_proc_t));
    com_mm_vmm_destroy_context(proc_vmm_ctx);
}

int com_sys_proc_next_fd(com_proc_t *proc) {
    kspinlock_acquire(&proc->fd_lock);
    int ret = proc->next_fd;

    // Update next_fd to point to the first free file descriptor
    proc->next_fd++;
    proc_update_fd_hint(proc);

    kspinlock_release(&proc->fd_lock);

    if (ret >= CONFIG_OPEN_MAX) {
        return -1;
    }

    return ret;
}

com_filedesc_t *com_sys_proc_get_fildesc_nolock(com_proc_t *proc, int fd) {
    if (NULL == proc) {
        return NULL;
    }

    if (fd > CONFIG_OPEN_MAX || fd < 0) {
        return NULL;
    }

    com_filedesc_t *fildesc = &proc->fd[fd];
    return fildesc;
}

com_file_t *com_sys_proc_get_file(com_proc_t *proc, int fd) {
    kspinlock_acquire(&proc->fd_lock);
    com_file_t *file = com_sys_proc_get_file_nolock(proc, fd);
    kspinlock_release(&proc->fd_lock);
    return file;
}

com_file_t *com_sys_proc_get_file_nolock(com_proc_t *proc, int fd) {
    if (NULL == proc) {
        return NULL;
    }

    if (fd > CONFIG_OPEN_MAX || fd < 0) {
        return NULL;
    }

    com_file_t *file = proc->fd[fd].file;
    COM_FS_FILE_HOLD(file);
    return file;
}

int com_sys_proc_duplicate_file_nolock(com_proc_t *proc,
                                       int         new_fd,
                                       int         old_fd) {
    if (old_fd < 0 || old_fd > CONFIG_OPEN_MAX || new_fd < 0 ||
        new_fd > CONFIG_OPEN_MAX) {
        return -EINVAL;
    }

    for (; NULL != proc->fd[new_fd].file; new_fd++) {
        if (new_fd >= CONFIG_OPEN_MAX) {
            return -EMFILE;
        }
    }

    proc->fd[new_fd]       = proc->fd[old_fd];
    proc->fd[new_fd].flags = 0;
    proc_update_fd_hint(proc);

    COM_FS_FILE_HOLD(proc->fd[new_fd].file);
    KDEBUG("after dup, fd=%d/%d has ref=%d/%d",
           old_fd,
           new_fd,
           proc->fd[old_fd].file->num_ref,
           proc->fd[new_fd].file->num_ref);

    return new_fd;
}

int com_sys_proc_duplicate_file(com_proc_t *proc, int new_fd, int old_fd) {
    kspinlock_acquire(&proc->fd_lock);
    int ret = com_sys_proc_duplicate_file_nolock(proc, new_fd, old_fd);
    kspinlock_release(&proc->fd_lock);
    return ret;
}

int com_sys_proc_close_file_nolock(com_proc_t *proc, int fd) {
    if (fd < 0 || fd > CONFIG_OPEN_MAX) {
        return EBADF;
    }

    com_filedesc_t *fildesc = &proc->fd[fd];
    if (NULL == fildesc->file) {
        return EBADF;
    }

    COM_FS_FILE_RELEASE(fildesc->file);
    *fildesc = (com_filedesc_t){0};

    // Recycle file descriptors
    if (fd < proc->next_fd) {
        proc->next_fd = fd;
    }

    return 0;
}

int com_sys_proc_close_file(com_proc_t *proc, int fd) {
    kspinlock_acquire(&proc->fd_lock);
    int ret = com_sys_proc_close_file_nolock(proc, fd);
    kspinlock_release(&proc->fd_lock);
    return ret;
}

int com_sys_proc_get_directory(com_file_t  **dir_file,
                               com_vnode_t **dir,
                               com_proc_t   *proc,
                               int           dir_fd) {
    com_file_t  *tmp_dir_file = NULL;
    com_vnode_t *tmp_dir      = NULL;

    if (AT_FDCWD == dir_fd) {
        tmp_dir = atomic_load(&proc->cwd);
    } else {
        tmp_dir_file = com_sys_proc_get_file(proc, dir_fd);
        if (NULL != tmp_dir_file) {
            tmp_dir = tmp_dir_file->vnode;
        } else {
            return EBADF;
        }
    }

    *dir_file = tmp_dir_file;
    *dir      = tmp_dir;
    return 0;
}

com_proc_t *com_sys_proc_get_by_pid(pid_t pid) {
    if (pid > CONFIG_PROC_MAX || pid < 1) {
        return NULL;
    }

    com_proc_t *ret = NULL;
    kspinlock_acquire(&PIDNamespaceLock);
    kradixtree_get_nolock((void *)&ret, &Processes, pid - 1);
    if (NULL != ret) {
        COM_SYS_PROC_HOLD(ret);
    }
    kspinlock_release(&PIDNamespaceLock);
    return ret;
}

com_proc_t *com_sys_proc_get_arbitrary_child(com_proc_t *proc) {
    kspinlock_acquire(&PIDNamespaceLock);

    com_proc_t *ret = NULL;
    for (size_t i = 0; i < CONFIG_PROC_MAX; i++) {
        com_proc_t *candidate;
        kradixtree_get_nolock((void *)&candidate, &Processes, i);

        if (NULL != candidate && candidate->parent_pid == proc->pid &&
            !candidate->exited) {
            COM_SYS_PROC_HOLD(candidate);
            ret = candidate;
            goto end;
        }
    }

end:
    kspinlock_release(&PIDNamespaceLock);
    return ret;
}

void com_sys_proc_add_thread(com_proc_t *proc, com_thread_t *thread) {
    KASSERT(!proc->exited);
    kspinlock_acquire(&proc->threads_lock);
    TAILQ_INSERT_TAIL(&proc->threads, thread, proc_threads);
    COM_SYS_PROC_HOLD(proc);
    kspinlock_release(&proc->threads_lock);
}

void com_sys_proc_remove_thread(com_proc_t *proc, com_thread_t *thread) {
    kspinlock_acquire(&proc->threads_lock);
    com_sys_proc_remove_thread_nolock(proc, thread);
    kspinlock_release(&proc->threads_lock);
}

void com_sys_proc_remove_thread_nolock(com_proc_t        *proc,
                                       struct com_thread *thread) {
    TAILQ_REMOVE(&proc->threads, thread, proc_threads);
}

void com_sys_proc_kill_other_threads(com_proc_t *proc, com_thread_t *excluded) {
    kspinlock_acquire(&proc->threads_lock);
    com_sys_proc_kill_other_threads_nolock(proc, excluded);
    kspinlock_release(&proc->threads_lock);
}

void com_sys_proc_kill_other_threads_nolock(com_proc_t   *proc,
                                            com_thread_t *excluded) {
    com_thread_t *t, *_;
    TAILQ_FOREACH_SAFE(t, &proc->threads, proc_threads, _) {
        if (t->tid != excluded->tid) {
            kspinlock_acquire(&t->sched_lock);
            com_sys_thread_exit_nolock(t);
            com_sys_proc_remove_thread_nolock(proc, t);
            kspinlock_release(&t->sched_lock);
        }
    }
}

void com_sys_proc_exit(com_proc_t *proc, int status) {
    kspinlock_acquire(&proc->signal_lock);

    kspinlock_acquire(&proc->fd_lock);
    for (int i = 0; i < CONFIG_OPEN_MAX; i++) {
        if (NULL != proc->fd[i].file) {
            COM_FS_FILE_RELEASE(proc->fd[i].file);
        }
    }
    kspinlock_release(&proc->fd_lock);

    proc->exited      = true;
    proc->exit_status = status;
    kspinlock_release(&proc->signal_lock);

    ksync_notify(&proc->waitpid_condvar, &proc->waitpid_waitlist);
    com_ipc_signal_send_to_proc(proc->parent_pid, SIGCHLD, proc);

    KDEBUG("pid=%d exited with code %d", proc->pid, status);
}

void com_sys_proc_stop(com_proc_t *proc, int stop_signal) {
    KASSERT(COM_IPC_SIGNAL_NONE != stop_signal);

    if (COM_IPC_SIGNAL_NONE != proc->stop_signal) {
        return;
    }

    proc->stop_signal   = stop_signal;
    proc->stop_notified = false;

    com_thread_t *t, *_;
    kspinlock_acquire(&proc->threads_lock);
    TAILQ_FOREACH_SAFE(t, &proc->threads, proc_threads, _) {
        com_ipc_signal_send_to_thread(t, stop_signal, proc);
    }
    kspinlock_release(&proc->threads_lock);

    ksync_notify(&proc->waitpid_condvar, &proc->waitpid_waitlist);
    com_ipc_signal_send_to_proc(proc->parent_pid, SIGCHLD, proc);
}

void com_sys_proc_terminate(com_proc_t *proc, int ecode) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    KASSERT(curr_thread->proc == proc);

    com_sys_proc_kill_other_threads(proc, curr_thread);
    com_sys_proc_exit(proc, ecode);
    com_sys_proc_remove_thread(proc, curr_thread);
    com_sys_thread_exit(curr_thread);
}

void com_sys_proc_hide(com_proc_t *proc) {
    kspinlock_acquire(&PIDNamespaceLock);
    // TODO: add it to new reaper thread queue
    // the new reaper thread will work differently from the previous: since we
    // have this function now, we can just have other parts drop their refcounts
    // and we'll always know when it hits 0 and we can dispatch the reaper
    // thread accordingly without hash tables or scheduler magic
    kradixtree_remove_nolock(&Processes, proc->pid - 1);
    kspinlock_acquire(&proc->pg_lock);
    if (NULL != proc->proc_group) {
        kspinlock_acquire(&proc->proc_group->procs_lock);
        TAILQ_REMOVE(&proc->proc_group->procs, proc, procs);
        kspinlock_release(&proc->proc_group->procs_lock);
    }
    proc->proc_group = NULL;
    kspinlock_release(&proc->pg_lock);
    kspinlock_release(&PIDNamespaceLock);
}

com_proc_group_t *com_sys_proc_new_group(com_proc_t         *leader,
                                         com_proc_session_t *session) {
    kspinlock_acquire(&ProcGroupMap.lock);
    com_proc_group_t *group = com_mm_slab_alloc(sizeof(com_proc_group_t));
    group->procs_lock       = KSPINLOCK_NEW();
    group->pgid             = leader->pid;
    group->session          = session;
    TAILQ_INIT(&group->procs);

    if (NULL == session && NULL != leader->proc_group) {
        group->session = leader->proc_group->session;
    }

    KASSERT(0 == kradixtree_put_nolock(&ProcGroupMap, group->pgid, group));
    KDEBUG("created pgid=%d", group->pgid);
    kspinlock_release(&ProcGroupMap.lock);
    return group;
}

int com_sys_proc_join_group(com_proc_t *proc, com_proc_group_t *group) {
    kspinlock_acquire(&proc->pg_lock);
    int ret = com_sys_proc_join_group_nolock(proc, group);
    kspinlock_release(&proc->pg_lock);
    return ret;
}

int com_sys_proc_join_group_nolock(com_proc_t *proc, com_proc_group_t *group) {
    if (NULL != proc->proc_group) {
        KASSERT(NULL != proc->proc_group->session);
        KASSERT(NULL != group->session);

        /*if (proc->proc_group->session->sid != proc->pid &&
            proc->proc_group->session->sid != group->session->sid) {
            return EPERM;
        }*/

        kspinlock_acquire(&proc->proc_group->procs_lock);
        TAILQ_REMOVE(&proc->proc_group->procs, proc, procs);
        kspinlock_release(&proc->proc_group->procs_lock);
    }

    kspinlock_acquire(&group->procs_lock);
    TAILQ_INSERT_TAIL(&group->procs, proc, procs);
    proc->proc_group = group;
    KDEBUG("pid=%d is now part of group pgid=%d", proc->pid, group->pgid);
    kspinlock_release(&group->procs_lock);
    return 0;
}

com_proc_group_t *com_sys_proc_get_group_by_pgid(pid_t pgid) {
    kspinlock_acquire(&ProcGroupMap.lock);
    com_proc_group_t *group;
    if (0 != kradixtree_get_nolock((void **)&group, &ProcGroupMap, pgid)) {
        group = NULL;
    }
    kspinlock_release(&ProcGroupMap.lock);
    return group;
}

com_proc_session_t *com_sys_proc_new_session_nolock(com_proc_t  *leader,
                                                    com_vnode_t *tty) {
    com_proc_session_t *session = com_mm_slab_alloc(sizeof(com_proc_session_t));
    if (NULL != tty) {
        COM_FS_VFS_VNODE_HOLD(tty);
    }
    session->tty     = tty;
    session->pg_lock = KSPINLOCK_NEW();
    session->sid     = leader->pid;
    TAILQ_INIT(&session->proc_groups);

    if (NULL != leader->proc_group) {
        kspinlock_acquire(&leader->proc_group->procs_lock);
        TAILQ_REMOVE(&leader->proc_group->procs, leader, procs);
        kspinlock_release(&leader->proc_group->procs_lock);
    }

    com_proc_group_t *new_group = com_sys_proc_new_group(leader, session);
    com_sys_proc_join_group_nolock(leader, new_group);

    KDEBUG("pid=%d is now leader of session sid=%d", leader->pid, session->sid);
    return session;
}

void com_sys_proc_init(void) {
    // 2 layers of radix tree are sufficient to hold like 260K processes on a
    // 64-bit system, so they should be enough. More layers may break
    // compatibility and cause lookup overhead
    KRADIXTREE_INIT(&Processes, 2);
    KRADIXTREE_INIT(&ProcGroupMap, 2);
}
