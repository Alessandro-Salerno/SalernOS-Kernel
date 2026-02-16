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

#pragma once

#include <vendor/tailq.h>
struct com_proc;
struct com_proc_group;
TAILQ_HEAD(com_proc_tailq, com_proc);
TAILQ_HEAD(com_proc_group_tailq, com_proc_group);

#include <arch/mmu.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/ipc/signal.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/thread.h>
#include <lib/spinlock.h>
#include <lib/sync.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define COM_SYS_PROC_HOLD(proc) \
    __atomic_add_fetch(&(proc)->num_ref, 1, __ATOMIC_RELAXED);
#define COM_SYS_PROC_RELEASE(proc)                                         \
    if (0 == __atomic_add_fetch(&(proc)->num_ref, -1, __ATOMIC_ACQ_REL)) { \
        com_sys_proc_hide(proc);                                           \
    }

typedef struct com_proc_session {
    pid_t                       sid;
    com_vnode_t                *tty;
    struct com_proc_group_tailq proc_groups;
    kspinlock_t                 pg_lock;
} com_proc_session_t;

typedef struct com_proc_group {
    pid_t                 pgid;
    com_proc_session_t   *session;
    struct com_proc_tailq procs;
    kspinlock_t           procs_lock;
    TAILQ_ENTRY(com_proc_group) proc_groups;
} com_proc_group_t;

typedef struct com_proc {
    pid_t              pid;
    pid_t              parent_pid;
    com_vmm_context_t *vmm_context;
    size_t             num_children;

    // Threads
    kspinlock_t             threads_lock;
    struct com_thread_tailq threads;
    size_t                  num_ref;
    size_t                  num_running_threads;

    // Process groups
    kspinlock_t       pg_lock;
    com_proc_group_t *proc_group;
    TAILQ_ENTRY(com_proc) procs;
    bool did_execve;

    // Termination & signals
    // COM_IPC_SIGNAL_NONE if not stopped, signal number otherwise
    KCACHE_FRIENDLY kspinlock_t signal_lock;
    com_sigmask_t               pending_signals;
    bool                        exited;
    int                         stop_signal;
    bool                        stop_notified;
    int                         exit_status;
    struct com_waitlist         waitpid_waitlist;
    struct com_sigaction       *sigaction[NSIG];

    // File system
    KCACHE_FRIENDLY com_vnode_t *root;
    _Atomic(com_vnode_t *)       cwd;
    kspinlock_t                  fd_lock;
    int                          next_fd;
    com_filedesc_t               fd[CONFIG_OPEN_MAX];
} com_proc_t;

com_proc_t *com_sys_proc_new(com_vmm_context_t *vmm_context,
                             pid_t              parent_pid,
                             com_vnode_t       *root,
                             com_vnode_t       *cwd);
void        com_sys_proc_destroy(com_proc_t *proc);

// File management
int             com_sys_proc_next_fd(com_proc_t *proc);
com_filedesc_t *com_sys_proc_get_fildesc_nolock(com_proc_t *proc, int fd);
com_file_t     *com_sys_proc_get_file(com_proc_t *proc, int fd);
com_file_t     *com_sys_proc_get_file_nolock(com_proc_t *proc, int fd);
int             com_sys_proc_duplicate_file_nolock(com_proc_t *proc,
                                                   int         new_fd,
                                                   int         old_fd);
int com_sys_proc_duplicate_file(com_proc_t *proc, int new_fd, int old_fd);
int com_sys_proc_close_file_nolock(com_proc_t *proc, int fd);
int com_sys_proc_close_file(com_proc_t *proc, int fd);
int com_sys_proc_get_directory(com_file_t  **dir_file,
                               com_vnode_t **dir,
                               com_proc_t   *proc,
                               int           dir_fd);
com_proc_t *com_sys_proc_get_by_pid(pid_t pid);
com_proc_t *com_sys_proc_get_arbitrary_child(com_proc_t *proc);

// Threads
void com_sys_proc_add_thread(com_proc_t *proc, struct com_thread *thread);
void com_sys_proc_remove_thread(com_proc_t *proc, struct com_thread *thread);
void com_sys_proc_remove_thread_nolock(com_proc_t        *proc,
                                       struct com_thread *thread);
void com_sys_proc_kill_other_threads(com_proc_t        *proc,
                                     struct com_thread *excluded);
void com_sys_proc_kill_other_threads_nolock(com_proc_t        *proc,
                                            struct com_thread *excluded);

void com_sys_proc_exit(com_proc_t *proc, int status);
void com_sys_proc_stop_nolock(com_proc_t *proc, int stop_signal);
void com_sys_proc_terminate(com_proc_t *proc, int ecode);
void com_sys_proc_hide(com_proc_t *proc);

com_proc_group_t *com_sys_proc_new_group(com_proc_t         *leader,
                                         com_proc_session_t *session);
int com_sys_proc_join_group(com_proc_t *proc, com_proc_group_t *group);
int com_sys_proc_join_group_nolock(com_proc_t *proc, com_proc_group_t *group);
com_proc_group_t   *com_sys_proc_get_group_by_pgid(pid_t pgid);
com_proc_session_t *com_sys_proc_new_session_nolock(com_proc_t  *leader,
                                                    com_vnode_t *tty);
void                com_sys_proc_init(void);
