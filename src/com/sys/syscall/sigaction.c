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

    int              sig  = COM_SYS_SYSCALL_ARG(int, 1);
    com_sigaction_t *act  = COM_SYS_SYSCALL_ARG(void *, 2);
    com_sigaction_t *oact = COM_SYS_SYSCALL_ARG(void *, 3);

    if (sig < 1 || sig > NSIG) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    com_proc_t *curr_proc = ARCH_CPU_GET_THREAD()->proc;
    kspinlock_acquire(&curr_proc->signal_lock);

    if (NULL != oact) {
        if (NULL != curr_proc->sigaction[sig]) {
            *oact = *curr_proc->sigaction[sig];
        } else {
            com_sigaction_t sa = {.sa_action = SIG_DFL};
            *oact              = sa;
        }
    }

    if (NULL != act) {
        if (NULL != curr_proc->sigaction[sig]) {
            com_mm_slab_free(curr_proc->sigaction[sig],
                             sizeof(com_sigaction_t));
        }

        com_sigaction_t *new_act = com_mm_slab_alloc(sizeof(com_sigaction_t));
        kmemcpy(new_act, act, sizeof(com_sigaction_t));
        curr_proc->sigaction[sig] = new_act;
    }

    kspinlock_release(&curr_proc->signal_lock);
    return COM_SYS_SYSCALL_OK(0);
}
