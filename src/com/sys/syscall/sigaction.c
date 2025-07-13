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
#include <kernel/com/sys/syscall.h>
#include <signal.h>

// SYSCALL: sigaction(int sig, struct sigaction *act, struct sigaction *oact)
COM_SYS_SYSCALL(com_sys_syscall_sigaction) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int               sig  = COM_SYS_SYSCALL_ARG(int, 1);
    struct sigaction *act  = COM_SYS_SYSCALL_ARG(void *, 2);
    struct sigaction *oact = COM_SYS_SYSCALL_ARG(void *, 3);

    if (sig < 1 || sig > NSIG) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    com_proc_t       *curr_proc = hdr_arch_cpu_get_thread()->proc;
    struct sigaction *tmp       = NULL;

    if (NULL != act) {
        tmp = __atomic_exchange_n(
            &curr_proc->sigaction[sig], act, __ATOMIC_SEQ_CST);
    }

    if (NULL != oact) {
        if (NULL == tmp) {
            tmp = __atomic_load_n(&curr_proc->sigaction[sig], __ATOMIC_SEQ_CST);
        }
        *oact = *tmp;
    }

    return COM_SYS_SYSCALL_OK(0);
}
