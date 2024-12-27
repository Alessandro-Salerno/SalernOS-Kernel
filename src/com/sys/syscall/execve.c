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

#include <arch/cpu.h>
#include <arch/info.h>
#include <arch/mmu.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/elf.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/mmu.h>
#include <lib/str.h>
#include <stddef.h>
#include <stdint.h>

com_syscall_ret_t com_sys_syscall_execve(arch_context_t *ctx,
                                         uintmax_t       pathptr,
                                         uintmax_t       argvptr,
                                         uintmax_t       envptr,
                                         uintmax_t       unused) {
  (void)unused;

  const char  *path = (void *)pathptr;
  char *const *argv = (void *)argvptr;
  char *const *env  = (void *)envptr;

  com_syscall_ret_t     ret    = {0};
  com_proc_t           *proc   = hdr_arch_cpu_get_thread()->proc;
  arch_mmu_pagetable_t *new_pt = arch_mmu_new_table();

  com_elf_data_t prog_data = {0};
  int            status    = com_sys_elf64_load(
      &prog_data, path, kstrlen(path), proc->root, proc->cwd, 0, new_pt);

  if (0 != status) {
    goto fail;
  }

  uintptr_t entry = prog_data.entry;

  if (0 != prog_data.interpreter_path[0]) {
    com_elf_data_t interp_data = {0};
    status                     = com_sys_elf64_load(&interp_data,
                                prog_data.interpreter_path,
                                kstrlen(prog_data.interpreter_path),
                                proc->root,
                                proc->cwd,
                                0x40000000, // TODO: why?
                                new_pt);

    if (0 != status) {
      goto fail;
    }

    entry = interp_data.entry;
  }

  uintptr_t stack_end   = 0x60000000; // TODO: why?
  uintptr_t stack_start = stack_end - (ARCH_PAGE_SIZE * 64);
  void     *stack_phys  = NULL;

  for (uintptr_t curr = stack_start; curr < stack_end; curr += ARCH_PAGE_SIZE) {
    stack_phys = com_mm_pmm_alloc();
    arch_mmu_map(new_pt,
                 (void *)curr,
                 stack_phys,
                 ARCH_MMU_FLAGS_READ | ARCH_MMU_FLAGS_WRITE |
                     ARCH_MMU_FLAGS_NOEXEC | ARCH_MMU_FLAGS_USER);
  }

  *ctx                = (arch_context_t){0};
  uintptr_t stack_ptr = com_sys_elf64_prepare_stack(
      prog_data, (uintptr_t)stack_phys + ARCH_PAGE_SIZE, stack_end, argv, env);
  ARCH_CONTEXT_THREAD_SET((*ctx), stack_ptr, 0, entry);

  arch_mmu_switch(new_pt);
  // arch_mmu_destroy_table(proc->page_table);
  proc->page_table = new_pt;

  for (size_t i = 0; i < proc->next_fd; i++) {
    if (NULL != proc->fd[i].file && (FD_CLOEXEC & proc->fd[i].flags)) {
      COM_FS_FILE_RELEASE(proc->fd[i].file);
    }
  }

  return ret;

fail:
  ret.err = status;
  return ret;
}
