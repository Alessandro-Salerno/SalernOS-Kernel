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
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/signal.h>
#include <kernel/com/sys/syscall.h>

// SYSCALL: kill(pid_t pid, int sig)
COM_SYS_SYSCALL(com_sys_syscall_kill) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    pid_t pid = COM_SYS_SYSCALL_ARG(pid_t, 1);
    int   sig = COM_SYS_SYSCALL_ARG(int, 2);

    com_syscall_ret_t ret         = {0, 0};
    com_thread_t     *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t       *curr_proc   = curr_thread->proc;

    if (0 == pid) {
        pid = curr_proc->proc_group->pgid;
    }

    if (-1 == pid) {
        // TODO: implement signal to all
        return COM_SYS_SYSCALL_ERR(ENOSYS);
    }

    if (0 == sig) {
        goto skip_send;
    }

    if (pid > 0) {
        ret.err = com_sys_signal_send_to_proc(pid, sig, curr_proc);
    } else {
        ret.err = com_sys_signal_send_to_proc_group(pid * -1, sig, curr_proc);
    }

skip_send:
    if (0 != ret.err) {
        ret.value = -1;
    }

    return ret;
}
