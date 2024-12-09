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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint64_t              pid;
  uint64_t              parent_pid;
  bool                  exited;
  int                   exit_status;
  arch_mmu_pagetable_t *page_table;
  size_t                num_children;
} com_proc_t;

com_proc_t *com_sys_proc_new(arch_mmu_pagetable_t *page_table,
                             uintmax_t             parent_pid);

void com_sys_proc_destroy(com_proc_t *proc);
