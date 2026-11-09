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

static void proc_notify_parent(com_proc_t *proc) {
    com_proc_t *parent = com_sys_proc_get_by_pid(proc->parent_pid);
    if (NULL != parent) {
        kspinlock_acquire(&parent->waitpid_lock);
        com_sys_sched_notify_all(&parent->waitpid_waitlist);
        kspinlock_release(&parent->waitpid_lock);
        COM_SYS_PROC_RELEASE(parent);
    }
}

static bool fd_free(com_proc_t *proc, int fd) {
    return NULL == proc->fd[fd].file && !proc->fd[fd].reserved;
}

static void proc_update_fd_hint(com_proc_t *proc) {
    // Best case: there's a file after the hint
    for (; proc->next_fd < CONFIG_OPEN_MAX; proc->next_fd++) {
        if (fd_free(proc, proc->next_fd)) {
            goto end;
        }
    }

    // Worst case: retry from the start, files might have been liberated without
    // calling close
    for (proc->next_fd = 0;
         proc->next_fd < CONFIG_OPEN_MAX && !fd_free(proc, proc->next_fd);
         proc->next_fd++);
end:
    proc->max_fd = KMAX(proc->max_fd, proc->next_fd);
}

com_proc_t *com_sys_proc_new(com_vmm_context_t *vmm_context,
                             pid_t              parent_pid,
                             com_vnode_t       *root,
                             com_vnode_t       *cwd) {
    com_proc_t *proc      = com_mm_slab_alloc(sizeof(com_proc_t));
    proc->vmm_context     = vmm_context;
    proc->state           = E_COM_PROC_STATE_RUNNING;
    proc->state_cause_sig = COM_IPC_SIGNAL_NONE;
    proc->state_notified  = false;
    proc->exit_code       = 0;
    proc->num_children    = 0;
    proc->parent_pid      = parent_pid;
    COM_FS_VFS_VNODE_HOLD(root);
    COM_FS_VFS_VNODE_HOLD(cwd);
    proc->root                = root;
    proc->cwd                 = cwd;
    proc->num_ref             = 1; // 1 because of the parent
    proc->num_running_threads = 0;
    proc->waitpid_lock        = KSPINLOCK_NEW();
    COM_SYS_THREAD_WAITLIST_INIT(&proc->waitpid_waitlist);
    COM_SYS_THREAD_WAITLIST_INIT(&proc->stop_waitlist);

    proc->threads_lock = KSPINLOCK_NEW();
    TAILQ_INIT(&proc->threads);
    COM_SYS_THREAD_WAITLIST_INIT(&proc->threads_waitlist);
    proc->threads_killer = NULL;

    kspinlock_acquire(&Processes.lock);
    KASSERT(NextPid <= CONFIG_PROC_MAX);
    proc->pid = NextPid++;
    kradixtree_put_nolock(&Processes, proc->pid - 1, proc);
    kspinlock_release(&Processes.lock);

    proc->fd_lock = KSPINLOCK_NEW();
    proc->next_fd = 0;
    proc->max_fd  = 0;

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
    KASSERT(E_COM_PROC_STATE_EXITED == proc->state);

    com_vmm_context_t *proc_vmm_ctx = proc->vmm_context;
    com_mm_slab_free(proc, sizeof(com_proc_t));
    com_mm_vmm_destroy_context(proc_vmm_ctx);
}

int com_sys_proc_next_fd(com_proc_t *proc) {
    kspinlock_acquire(&proc->fd_lock);
    if (proc->next_fd >= CONFIG_OPEN_MAX || !fd_free(proc, proc->next_fd)) {
        proc_update_fd_hint(proc);
    }

    int ret = proc->next_fd;
    if (ret < CONFIG_OPEN_MAX) {
        proc->fd[ret].reserved = true;
        proc->next_fd++;
        proc_update_fd_hint(proc);
    } else {
        ret = -1;
    }

    kspinlock_release(&proc->fd_lock);
    return ret;
}

void com_sys_proc_install_fd(com_proc_t *proc,
                             int         fd,
                             com_file_t *file,
                             uintmax_t   fd_flags) {
    kspinlock_acquire(&proc->fd_lock);
    KASSERT(proc->fd[fd].reserved && NULL == proc->fd[fd].file);
    proc->fd[fd] = (com_filedesc_t){.file = file, .flags = fd_flags};
    kspinlock_release(&proc->fd_lock);
}

void com_sys_proc_unreserve_fd(com_proc_t *proc, int fd) {
    kspinlock_acquire(&proc->fd_lock);
    KASSERT(proc->fd[fd].reserved && NULL == proc->fd[fd].file);
    proc->fd[fd].reserved = false;
    if (fd < proc->next_fd) {
        proc->next_fd = fd;
    }
    kspinlock_release(&proc->fd_lock);
}

com_filedesc_t *com_sys_proc_get_fildesc_nolock(com_proc_t *proc, int fd) {
    if (NULL == proc) {
        return NULL;
    }

    if (fd >= CONFIG_OPEN_MAX || fd < 0) {
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

    if (fd >= CONFIG_OPEN_MAX || fd < 0) {
        return NULL;
    }

    com_file_t *file = proc->fd[fd].file;
    COM_FS_FILE_HOLD(file);
    return file;
}

int com_sys_proc_duplicate_file_nolock(com_proc_t *proc,
                                       int         new_fd,
                                       int         old_fd) {
    if (old_fd < 0 || old_fd >= CONFIG_OPEN_MAX || new_fd < 0 ||
        new_fd >= CONFIG_OPEN_MAX) {
        return -EINVAL;
    }

    while (new_fd < CONFIG_OPEN_MAX && !fd_free(proc, new_fd)) {
        new_fd++;
    }

    if (new_fd >= CONFIG_OPEN_MAX) {
        return -EMFILE;
    }

    proc->fd[new_fd]       = proc->fd[old_fd];
    proc->fd[new_fd].flags = 0;
    proc->max_fd           = KMAX(proc->max_fd, new_fd + 1);
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

int com_sys_proc_close_file_nolock(com_file_t **out_file,
                                   com_proc_t  *proc,
                                   int          fd) {
    if (fd < 0 || fd >= CONFIG_OPEN_MAX) {
        return EBADF;
    }

    com_filedesc_t *fildesc = &proc->fd[fd];
    if (NULL == fildesc->file) {
        return EBADF;
    }

    *out_file = fildesc->file;
    *fildesc  = (com_filedesc_t){0};

    // Recycle file descriptors
    if (fd < proc->next_fd) {
        proc->next_fd = fd;
    }

    return 0;
}

int com_sys_proc_close_file(com_proc_t *proc, int fd) {
    kspinlock_acquire(&proc->fd_lock);
    com_file_t *file = NULL;
    int         ret  = com_sys_proc_close_file_nolock(&file, proc, fd);
    kspinlock_release(&proc->fd_lock);
    COM_FS_FILE_RELEASE(file);
    return ret;
}

com_vnode_t *com_sys_proc_get_cwd(com_proc_t *proc) {
    kspinlock_acquire(&proc->fd_lock);
    com_vnode_t *cwd = proc->cwd;
    COM_FS_VFS_VNODE_HOLD(cwd);
    kspinlock_release(&proc->fd_lock);
    return cwd;
}

void com_sys_proc_set_cwd(com_proc_t *proc, com_vnode_t *cwd) {
    COM_FS_VFS_VNODE_HOLD(cwd);
    kspinlock_acquire(&proc->fd_lock);
    com_vnode_t *old = proc->cwd;
    proc->cwd        = cwd;
    kspinlock_release(&proc->fd_lock);

    // Outside the spinlock, since it may free the vnode
    COM_FS_VFS_VNODE_RELEASE(old);
}

int com_sys_proc_get_directory(com_vnode_t **dir,
                               com_proc_t   *proc,
                               int           dir_fd) {
    if (AT_FDCWD == dir_fd) {
        *dir = com_sys_proc_get_cwd(proc);
        return 0;
    }

    com_file_t *file = com_sys_proc_get_file(proc, dir_fd);
    if (NULL == file) {
        *dir = NULL;
        return EBADF;
    }

    *dir = file->vnode;
    COM_FS_VFS_VNODE_HOLD(*dir);
    COM_FS_FILE_RELEASE(file);
    return 0;
}

// A count of 0 means that the last holder is on its way to com_sys_proc_hide. A
// new reference would make the next release hide the process a second time
static bool proc_hold_if_alive(com_proc_t *proc) {
    size_t num_ref = __atomic_load_n(&proc->num_ref, __ATOMIC_RELAXED);
    while (0 != num_ref) {
        if (__atomic_compare_exchange_n(&proc->num_ref,
                                        &num_ref,
                                        num_ref + 1,
                                        false,
                                        __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
            return true;
        }
    }
    return false;
}

com_proc_t *com_sys_proc_get_by_pid(pid_t pid) {
    if (pid > CONFIG_PROC_MAX || pid < 1) {
        return NULL;
    }

    com_proc_t *ret = NULL;
    kspinlock_acquire(&PIDNamespaceLock);
    kradixtree_get_nolock((void *)&ret, &Processes, pid - 1);
    if (NULL != ret && !proc_hold_if_alive(ret)) {
        ret = NULL;
    }
    kspinlock_release(&PIDNamespaceLock);
    return ret;
}

com_proc_t *com_sys_proc_get_arbitrary_child(com_proc_t *proc) {
    kspinlock_acquire(&PIDNamespaceLock);

    com_proc_t *ret = NULL;
    for (int i = 0; i < NextPid + 1; i++) {
        com_proc_t *candidate = NULL;
        kradixtree_get_nolock((void *)&candidate, &Processes, i);

        if (NULL != candidate && candidate->parent_pid == proc->pid) {
            kspinlock_acquire(&candidate->signal_lock);
            bool eligible = E_COM_PROC_STATE_EXITED != candidate->state ||
                            !candidate->state_notified;
            bool ready    = (E_COM_PROC_STATE_EXITED == candidate->state ||
                             E_COM_PROC_STATE_STOPPED == candidate->state) &&
                            !candidate->state_notified;
            kspinlock_release(&candidate->signal_lock);
            if (eligible && (NULL == ret || ready)) {
                ret = candidate;
            }
            if (eligible && ready) {
                break;
            }
        }
    }

    if (NULL != ret) {
        COM_SYS_PROC_HOLD(ret);
    }
    kspinlock_release(&PIDNamespaceLock);
    return ret;
}

bool com_sys_proc_threads_alive_nolock(com_proc_t *proc, com_thread_t *except) {
    com_thread_t *thread = TAILQ_FIRST(&proc->threads);
    return NULL != thread &&
           (thread != except || NULL != TAILQ_NEXT(thread, proc_threads));
}

bool com_sys_proc_all_threads_exiting_nolock(com_proc_t *proc) {
    com_thread_t *thread;
    TAILQ_FOREACH(thread, &proc->threads, proc_threads) {
        if (!__atomic_load_n(&thread->exit_requested, __ATOMIC_ACQUIRE)) {
            return false;
        }
    }
    return true;
}

void com_sys_proc_add_thread(com_proc_t *proc, com_thread_t *thread) {
    KASSERT(E_COM_PROC_STATE_EXITED != proc->state);
    kspinlock_acquire(&proc->threads_lock);
    TAILQ_INSERT_TAIL(&proc->threads, thread, proc_threads);
    COM_SYS_PROC_HOLD(proc);
    kspinlock_release(&proc->threads_lock);
}

void com_sys_proc_remove_thread(com_proc_t *proc, com_thread_t *thread) {
    kspinlock_acquire(&proc->threads_lock);
    com_sys_proc_remove_thread_nolock(proc, thread);
    kspinlock_release(&proc->threads_lock);
    com_sys_sched_notify_all(&proc->threads_waitlist);
}

void com_sys_proc_remove_thread_nolock(com_proc_t        *proc,
                                       struct com_thread *thread) {
    TAILQ_REMOVE(&proc->threads, thread, proc_threads);
}

void com_sys_proc_kill_other_threads(void) {
    com_thread_t *curr_thraed = ARCH_CPU_GET_THREAD();
    KASSERT(NULL != curr_thraed);
    com_proc_t *curr_proc = curr_thraed->proc;
    KASSERT(NULL != curr_proc);

    kspinlock_acquire(&curr_proc->threads_lock);
    com_sys_proc_kill_other_threads_nolock();
    kspinlock_release(&curr_proc->threads_lock);
}

// Caller holds threads_lock. Wait for switch-out acknowledgement before
// releasing process resources or replacing its address space during exec
void com_sys_proc_kill_other_threads_nolock(void) {
    com_thread_t *curr = ARCH_CPU_GET_THREAD();
    com_proc_t   *proc = curr->proc;
    if (NULL != proc->threads_killer && proc->threads_killer != curr) {
        kspinlock_release(&proc->threads_lock);
        com_sys_thread_exit(curr);
        __builtin_unreachable();
    }
    proc->threads_killer = curr;
    com_thread_t *thread;
    TAILQ_FOREACH(thread, &proc->threads, proc_threads) {
        if (thread != curr) {
            com_sys_thread_request_exit(thread);
        }
    }
    while (com_sys_proc_threads_alive_nolock(proc, curr)) {
        kspinlock_wait(&proc->threads_lock, &proc->threads_waitlist, 0, false);
    }
    proc->threads_killer = NULL;
}

void com_sys_proc_transition_nolock(com_proc_t      *proc,
                                    com_proc_state_t state,
                                    int              cause_sig) {
    switch (state) {
        case E_COM_PROC_STATE_RUNNING:
            KASSERT(E_COM_PROC_STATE_STOPPED == proc->state);
            break;
        case E_COM_PROC_STATE_STOPPED:
            KASSERT(E_COM_PROC_STATE_RUNNING == proc->state);
            KASSERT(COM_IPC_SIGNAL_NONE != cause_sig);
            break;
        case E_COM_PROC_STATE_EXITED:
            KASSERT(E_COM_PROC_STATE_EXITED != proc->state);
            break;
        default:
            KASSERT(!"invalid argument");
            break;
    }

    proc->state           = state;
    proc->state_cause_sig = cause_sig;
    proc->state_notified  = false;
}

void com_sys_proc_exit(int exit_code, int cause_sig) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    KASSERT(NULL != curr_thread);
    com_proc_t *curr_proc = curr_thread->proc;
    KASSERT(NULL != curr_proc);
    KASSERT(1 == curr_proc->num_running_threads);

    // NOTE: we don't need to take the fd lock because we are guaranteed to be
    // the only thread left
    for (int i = 0; i < curr_proc->max_fd; i++) {
        com_file_t *file = curr_proc->fd[i].file;
        curr_proc->fd[i] = (com_filedesc_t){0};
        COM_FS_FILE_RELEASE(file);
    }

    com_vnode_t *cwd  = curr_proc->cwd;
    com_vnode_t *root = curr_proc->root;
    curr_proc->cwd = curr_proc->root = NULL;
    COM_FS_VFS_VNODE_RELEASE(cwd);
    COM_FS_VFS_VNODE_RELEASE(root);

    // A session loses its controlling terminal with the controlling process
    com_proc_session_t *session = curr_proc->proc_group->session;
    if (NULL != session && session->sid == curr_proc->pid) {
        kspinlock_acquire(&session->pg_lock);
        com_vnode_t *ctty = session->tty;
        __atomic_store_n(&session->tty, NULL, __ATOMIC_RELAXED);
        kspinlock_release(&session->pg_lock);
        COM_FS_VFS_VNODE_RELEASE(ctty);
    }

    // Publish status before notifying and never acquire authority under
    // signal_lock
    kspinlock_acquire(&curr_proc->signal_lock);
    curr_proc->exit_code = exit_code;
    com_sys_proc_transition_nolock(curr_proc,
                                   E_COM_PROC_STATE_EXITED,
                                   cause_sig);

    kspinlock_release(&curr_proc->signal_lock);
    proc_notify_parent(curr_proc);
    com_ipc_signal_send_to_proc(curr_proc->parent_pid, SIGCHLD, curr_proc);

    KDEBUG("pid=%d exited with code %d", curr_proc->pid, exit_code);
}

void com_sys_proc_stop_nolock(int stop_signal) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    KASSERT(NULL != curr_thread);
    com_proc_t *curr_proc = curr_thread->proc;
    KASSERT(NULL != curr_proc);
    KASSERT(COM_IPC_SIGNAL_NONE != stop_signal);

    if (E_COM_PROC_STATE_RUNNING != curr_proc->state) {
        return;
    }

    com_sys_proc_transition_nolock(curr_proc,
                                   E_COM_PROC_STATE_STOPPED,
                                   stop_signal);

    kspinlock_release(&curr_proc->signal_lock);
    com_thread_t *t, *_;
    kspinlock_acquire(&curr_proc->threads_lock);
    TAILQ_FOREACH_SAFE(t, &curr_proc->threads, proc_threads, _) {
        com_ipc_signal_send_to_thread(t, stop_signal, curr_proc);
    }
    kspinlock_release(&curr_proc->threads_lock);
    proc_notify_parent(curr_proc);
    com_ipc_signal_send_to_proc(curr_proc->parent_pid, SIGCHLD, curr_proc);
    kspinlock_acquire(&curr_proc->signal_lock);
}

void com_sys_proc_terminate(int exit_code, int cause_sig) {
    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    KASSERT(NULL != curr_thread);
    com_proc_t *curr_proc = curr_thread->proc;
    KASSERT(NULL != curr_proc);

    com_sys_proc_kill_other_threads();
    com_sys_proc_exit(exit_code, cause_sig);
    // NOTE: now the process is in an exited state, but this thread is still
    // running. We could be preempted here, so it's important that the scheduler
    // never refuses to run a thread if it's process has exited, because we want
    // to get back here and finish our job
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

    KASSERT_CALL(0,
                 ==,
                 kradixtree_put_nolock(&ProcGroupMap, group->pgid, group));
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
