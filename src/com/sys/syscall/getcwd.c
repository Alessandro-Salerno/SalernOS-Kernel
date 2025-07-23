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
#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/file.h>
#include <kernel/com/fs/vfs.h>
#include <kernel/com/spinlock.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

// SYSCALL: getcwd(char *buf, size_t size)
COM_SYS_SYSCALL(com_sys_syscall_getcwd) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    char  *buf  = COM_SYS_SYSCALL_ARG(char *, 1);
    size_t size = COM_SYS_SYSCALL_ARG(size_t, 2);

    (void)size;
    buf[0] = '/';
    buf[1] = 0;

    return COM_SYS_SYSCALL_OK((uintptr_t)buf);
}
