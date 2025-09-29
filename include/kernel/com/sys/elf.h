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

#include <kernel/com/fs/vfs.h>
#include <kernel/com/mm/vmm.h>
#include <kernel/com/sys/proc.h>
#include <kernel/platform/context.h>
#include <kernel/platform/info.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uintptr_t entry;
    size_t    phdr;
    size_t    phent_sz;
    size_t    phent_num;
    char      interpreter_path[64];
} com_elf_data_t;

int       com_sys_elf64_load(com_elf_data_t    *out,
                             const char        *exec_path,
                             size_t             exec_path_len,
                             com_vnode_t       *root,
                             com_vnode_t       *cwd,
                             uintptr_t          virt_off,
                             com_vmm_context_t *vmm_context);
uintptr_t com_sys_elf64_prepare_stack(com_elf_data_t elf_data,
                                      size_t         stack_end_phys,
                                      size_t         stack_end_virt,
                                      char *const    argv[],
                                      char *const    envp[]);
int       com_sys_elf64_prepare_proc(com_vmm_context_t **out_vmm_context,
                                     const char         *path,
                                     char *const         argv[],
                                     char *const         env[],
                                     com_proc_t         *proc,
                                     arch_context_t     *ctx);
