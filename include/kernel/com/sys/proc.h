/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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

#include <arch/mmu.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/sys/thread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct com_proc {
  uint64_t              pid;
  uint64_t              parent_pid;
  bool                  exited;
  int                   exit_status;
  arch_mmu_pagetable_t *page_table;
  size_t                num_children;

  com_vnode_t           *root;
  _Atomic(com_vnode_t *) cwd;
  // TODO: this is a lock... circular dependencies
  int            fd_lock;
  uintmax_t      next_fd;
  com_filedesc_t fd[16];

  struct com_thread_tailq waiting_on;
} com_proc_t;

com_proc_t *com_sys_proc_new(arch_mmu_pagetable_t *page_table,
                             uintmax_t             parent_pid,
                             com_vnode_t          *root,
                             com_vnode_t          *cwd);
void        com_sys_proc_destroy(com_proc_t *proc);
uintmax_t   com_sys_proc_next_fd(com_proc_t *proc);
com_file_t *com_sys_proc_get_file(com_proc_t *proc, uintmax_t fd);
com_proc_t *com_sys_proc_get_by_pid(uintmax_t pid);
void        com_sys_proc_acquire_glock(void);
void        com_sys_proc_release_glock(void);
void        com_sys_proc_wait(com_proc_t *proc);
void        com_sys_proc_exit(com_proc_t *proc, int status);
