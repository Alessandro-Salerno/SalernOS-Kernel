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

#pragma once

#include <vendor/tailq.h>
struct com_proc;
struct com_proc_group;
TAILQ_HEAD(com_proc_tailq, com_proc);
TAILQ_HEAD(com_proc_group_tailq, com_proc_group);

#include <arch/mmu.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/signal.h>
#include <kernel/com/sys/thread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define COM_SYS_PROC_MAX_FDS 64

typedef struct com_proc_session {
    pid_t                       sid;
    com_vnode_t                *tty;
    struct com_proc_group_tailq proc_groups;
    com_spinlock_t              pg_lock;
} com_proc_session_t;

typedef struct com_proc_group {
    pid_t                 pgid;
    com_proc_session_t   *session;
    struct com_proc_tailq procs;
    com_spinlock_t        procs_lock;
    TAILQ_ENTRY(com_proc_group) proc_groups;
} com_proc_group_t;

typedef struct com_proc {
    pid_t pid;
    pid_t parent_pid;
    bool  exited;
    int   stop_signal; // COM_SYS_SIGNAL_NONE if not stopped, signal number
                       // otherwise
    bool                  stop_notified;
    int                   exit_status;
    arch_mmu_pagetable_t *page_table;
    size_t                num_children;

    com_vnode_t           *root;
    _Atomic(com_vnode_t *) cwd;
    com_spinlock_t         fd_lock;
    int                    next_fd;
    com_filedesc_t         fd[COM_SYS_PROC_MAX_FDS];

    com_spinlock_t pages_lock;
    size_t         used_pages;

    struct com_thread_tailq notifications;

    com_spinlock_t          threads_lock;
    struct com_thread_tailq threads;
    size_t                  num_ref;

    com_spinlock_t    pg_lock;
    com_proc_group_t *proc_group;
    TAILQ_ENTRY(com_proc) procs;

    com_spinlock_t        signal_lock;
    struct com_sigaction *sigaction[NSIG];
    com_sigmask_t         pending_signals;
    com_sigmask_t         masked_signals;
} com_proc_t;

com_proc_t     *com_sys_proc_new(arch_mmu_pagetable_t *page_table,
                                 pid_t                 parent_pid,
                                 com_vnode_t          *root,
                                 com_vnode_t          *cwd);
void            com_sys_proc_destroy(com_proc_t *proc);
int             com_sys_proc_next_fd(com_proc_t *proc);
com_filedesc_t *com_sys_proc_get_fildesc(com_proc_t *proc, int fd);
com_file_t     *com_sys_proc_get_file(com_proc_t *proc, int fd);
int com_sys_proc_duplicate_file(com_proc_t *proc, int new_fd, int old_fd);
com_proc_t *com_sys_proc_get_by_pid(pid_t pid);
com_proc_t *com_sys_proc_get_arbitrary_child(com_proc_t *proc);
void        com_sys_proc_acquire_glock(void);
void        com_sys_proc_release_glock(void);
void        com_sys_proc_wait(com_proc_t *proc);
void com_sys_proc_add_thread(com_proc_t *proc, struct com_thread *thread);

void com_sys_proc_remove_thread(com_proc_t *proc, struct com_thread *thread);
void com_sys_proc_remove_thread_nolock(com_proc_t        *proc,
                                       struct com_thread *thread);
void com_sys_proc_exit(com_proc_t *proc, int status);
void com_sys_proc_stop(com_proc_t *proc, int stop_signal);
com_proc_group_t *com_sys_proc_new_group(com_proc_t         *leader,
                                         com_proc_session_t *session);
int com_sys_proc_join_group(com_proc_t *proc, com_proc_group_t *group);
int com_sys_proc_join_group_nolock(com_proc_t *proc, com_proc_group_t *group);
com_proc_group_t   *com_sys_proc_get_group_by_pgid(pid_t pgid);
com_proc_session_t *com_sys_proc_new_session_nolock(com_proc_t  *leader,
                                                    com_vnode_t *tty);
