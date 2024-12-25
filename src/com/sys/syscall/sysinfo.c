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

// TODO: Replace this syscall with procfs

#include <arch/cpu.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/syscall.h>
#include <lib/str.h>
#include <stdint.h>

struct sysinfo {
  char      cpu[64];
  char      gpu[64];
  char      kernel[64];
  uintmax_t used_mem;
  uintmax_t sys_mem;
};

com_syscall_ret_t com_sys_syscall_sysinfo(arch_context_t *ctx,
                                          uintmax_t       bufptr,
                                          uintmax_t       unused1,
                                          uintmax_t       unused2,
                                          uintmax_t       unused3) {
  (void)unused1;
  (void)unused2;
  (void)unused3;
  (void)ctx;

  struct sysinfo *sysinfo = (void *)bufptr;
  // kstrcpy(sysinfo->cpu, "Not implemented");
  // kstrcpy(sysinfo->gpu, "Not implemented");
  kstrcpy(sysinfo->kernel, "SalernOS Kernel 0.2.1");

  com_mm_pmm_get_info(&sysinfo->used_mem, NULL, NULL, &sysinfo->sys_mem, NULL);

  return (com_syscall_ret_t){0, 0};
}
