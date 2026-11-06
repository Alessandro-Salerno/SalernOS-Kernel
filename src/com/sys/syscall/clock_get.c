/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
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
#include <arch/mmu.h>
#include <errno.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/syscall.h>
#include <lib/usercopy.h>
#include <lib/util.h>
#include <time.h>

// SYSCALL: clock_get(int clock, struct timespec *ts)
COM_SYS_SYSCALL(com_sys_syscall_clock_get) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    int              clock = COM_SYS_SYSCALL_ARG(int, 1);
    struct timespec *ts    = COM_SYS_SYSCALL_ARG(void *, 2);

    (void)clock; // TODO: what is this?
    uintmax_t       ns  = ARCH_CPU_GET_TIME();
    struct timespec kts = {.tv_sec  = ns / KNANOS_PER_SEC,
                           .tv_nsec = ns % KNANOS_PER_SEC};

    int copy_ret = kmemcpy_to_user(ts, &kts, sizeof(kts));
    if (0 != copy_ret) {
        return COM_SYS_SYSCALL_ERR(copy_ret);
    }

    return COM_SYS_SYSCALL_OK(0);
}
