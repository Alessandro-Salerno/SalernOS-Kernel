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

#include <arch/cpu.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/sys/syscall.h>
#include <lib/mem.h>
#include <lib/str.h>
#include <stdint.h>

struct sysinfo {
    char      cpu[64];
    char      gpu[64];
    char      kernel[64];
    uintmax_t used_mem;
    uintmax_t sys_mem;
};

// SYSCALL: sysinfo(struct sysinfo *buf)
COM_SYS_SYSCALL(com_sys_syscall_sysinfo) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    struct sysinfo *sysinfo = COM_SYS_SYSCALL_ARG(struct sysinfo *, 1);

    // kstrcpy(sysinfo->cpu, "Not implemented");
    // kstrcpy(sysinfo->gpu, "Not implemented");
    kmemset(sysinfo->cpu, 64, 0);
    kmemset(sysinfo->gpu, 64, 0);
    kstrcpy(sysinfo->kernel, "SalernOS Kernel 0.2.3 (indev)");

    com_mm_pmm_get_info(
        &sysinfo->used_mem, NULL, NULL, &sysinfo->sys_mem, NULL);

    return COM_SYS_SYSCALL_OK(0);
}
