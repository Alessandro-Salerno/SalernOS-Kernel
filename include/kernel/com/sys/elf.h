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

#include <kernel/com/fs/vfs.h>
#include <kernel/platform/mmu.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uintptr_t entry;
  size_t    phdr;
  size_t    phent_sz;
  size_t    phent_num;
  char      interpreter_path[64];
} com_elf_data_t;

int       com_sys_elf64_load(com_elf_data_t       *out,
                             const char           *exec_path,
                             size_t                exec_path_len,
                             COM_FS_VFS_VNODE_t          *root,
                             COM_FS_VFS_VNODE_t          *cwd,
                             uintptr_t             virt_off,
                             arch_mmu_pagetable_t *pt);
uintptr_t com_sys_elf64_prepare_stack(com_elf_data_t elf_data,
                                      size_t         stack_end_phys,
                                      size_t         stack_end_virt,
                                      char *const    argv[],
                                      char *const    envp[]);
