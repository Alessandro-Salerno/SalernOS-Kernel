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

#include <arch/info.h>
#include <arch/mmu.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/platform/mmu.h>
#include <stdint.h>

static com_spinlock_t PidLock = COM_SPINLOCK_NEW();
static uintmax_t      NextPid = 1;

com_proc_t *com_sys_proc_new(arch_mmu_pagetable_t *page_table,
                             uintmax_t             parent_pid,
                             com_vnode_t          *root,
                             com_vnode_t          *cwd) {
  com_proc_t *proc   = (com_proc_t *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
  proc->page_table   = page_table;
  proc->exited       = false;
  proc->exit_status  = 0;
  proc->num_children = 0;
  proc->parent_pid   = parent_pid;
  proc->root         = root;
  proc->cwd          = cwd;

  // TODO: use atomic operations (faster)
  hdr_com_spinlock_acquire(&PidLock);
  proc->pid = NextPid++;
  hdr_com_spinlock_release(&PidLock);

  proc->fd_lock = COM_SPINLOCK_NEW();
  proc->next_fd = 0;

  return proc;
}

void com_sys_proc_destroy(com_proc_t *proc) {
  com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(proc));
}

uintmax_t com_sys_proc_next_fd(com_proc_t *proc) {
  hdr_com_spinlock_acquire(&proc->fd_lock);
  uintmax_t ret = proc->next_fd;
  proc->next_fd++;
  hdr_com_spinlock_release(&proc->fd_lock);
  return ret;
}
