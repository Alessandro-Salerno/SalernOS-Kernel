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

#include <arch/context.h>
#include <arch/cpu.h>
#include <arch/mmu.h>
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <kernel/platform/mmu.h>
#include <lib/str.h>
#include <stddef.h>
#include <stdint.h>

com_syscall_ret_t com_sys_syscall_fork(arch_context_t *ctx,
                                       uintmax_t       unused1,
                                       uintmax_t       unused2,
                                       uintmax_t       unused3,
                                       uintmax_t       unused4) {
  (void)unused1;
  (void)unused2;
  (void)unused3;
  (void)unused4;

  com_syscall_ret_t ret  = {0};
  com_proc_t       *proc = hdr_arch_cpu_get()->thread->proc;

  hdr_com_spinlock_acquire(&proc->fd_lock);
  // TODO: acquire pages lock (not yet implemented)

  arch_mmu_pagetable_t *new_pt = arch_mmu_duplicate_table(proc->page_table);

  COM_VNODE_HOLD(proc->root);
  com_proc_t *new_proc =
      com_sys_proc_new(new_pt, proc->pid, proc->root, proc->cwd);
  COM_VNODE_RELEASE(proc->root);
  // TODO: do things with fs-base and pages (not implemented yet)
  com_thread_t *new_thread = com_sys_thread_new(new_proc, NULL, 0, 0);

  if (NULL == new_pt || NULL == new_proc || NULL == new_thread) {
    ret.err = ENOMEM;
    goto end;
  }

  // TODO: same as execve, make this better than i < 16
  for (size_t i = 0; i < 16; i++) {
    if (NULL != proc->fd[i].file) {
      new_proc->fd[i].flags = proc->fd[i].flags;
      COM_FS_FILE_HOLD(proc->fd[i].file);
      new_proc->fd[i].file = proc->fd[i].file;
    }
  }

  ARCH_CONTEXT_FORK(new_thread, *ctx);

  hdr_com_spinlock_release(&proc->fd_lock);

  proc->num_children++;
  new_thread->runnable = false;

  com_sys_thread_ready(new_thread);

end:
  return ret;
}