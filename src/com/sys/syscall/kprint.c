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

#include <kernel/com/io/log.h>
#include <kernel/com/sys/syscall.h>
#include <lib/util.h>

// SYSCALL: kprint(const char *message)
COM_SYS_SYSCALL(com_sys_syscall_kprint) {
#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_USER
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    const char *message = COM_SYS_SYSCALL_ARG(const char *, 1);

    com_io_log_lock();
    kinitlog("USER", "\033[36m");
    com_io_log_puts_nolock(message);
    com_io_log_putc_nolock('\n');
    com_io_log_unlock();
#else
    COM_SYS_SYSCALL_UNUSED_START(0);
#endif

    return COM_SYS_SYSCALL_OK(0);
}
