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

#include <errno.h>
#include <kernel/com/io/log.h>
#include <kernel/com/sys/syscall.h>
#include <lib/usercopy.h>
#include <lib/util.h>

#include <salernos/syscall_str.h>

// SYSCALL: kprint(const struct syscall_str *message)
COM_SYS_SYSCALL(com_sys_syscall_kprint) {
#if CONFIG_LOG_LEVEL >= CONST_LOG_LEVEL_USER
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    const struct syscall_str *umessage = COM_SYS_SYSCALL_ARG(void *, 1);

    struct syscall_str message;
    int ret = kmemcpy_from_user(&message, umessage, sizeof(message));
    if (0 != ret) {
        return COM_SYS_SYSCALL_ERR(ret);
    }

    char   chunk[128];
    size_t done = 0;
    do {
        size_t n = KMIN(message.len - done, sizeof(chunk));
        ret      = kmemcpy_from_user(chunk, message.str + done, n);
        if (0 != ret) {
            return COM_SYS_SYSCALL_ERR(ret);
        }

        com_io_log_lock();
        if (0 == done) {
            kinitlog("USER", "\033[36m");
        }
        com_io_log_putsn_nolock(chunk, n);
        done += n;
        if (done == message.len) {
            com_io_log_putc_nolock('\n');
        }
        com_io_log_unlock();
    } while (done < message.len);
#else
    COM_SYS_SYSCALL_UNUSED_START(0);
#endif

    return COM_SYS_SYSCALL_OK(0);
}
