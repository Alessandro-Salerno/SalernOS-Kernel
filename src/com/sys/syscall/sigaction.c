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
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/usercopy.h>
#include <signal.h>

// SYSCALL: sigaction(int sig, struct sigaction *act, struct sigaction *oact)
COM_SYS_SYSCALL(com_sys_syscall_sigaction) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int              sig  = COM_SYS_SYSCALL_ARG(int, 1);
    com_sigaction_t *act  = COM_SYS_SYSCALL_ARG(void *, 2);
    com_sigaction_t *oact = COM_SYS_SYSCALL_ARG(void *, 3);

    if (sig < 1 || sig >= NSIG) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    if (NULL != act && (SIGKILL == sig || SIGSTOP == sig)) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    com_sigaction_t *new_act = NULL;
    if (NULL != act) {
        new_act      = com_mm_slab_alloc(sizeof(com_sigaction_t));
        int copy_ret = kmemcpy_from_user(new_act, act, sizeof(*new_act));
        // According to clankers, Linux does not error in this case. However,
        // there's no good reason for which a userspace process would want to
        // give us a non-user or otherwise invalid address, so we save the
        // assembly trickery and punish these gangsters with an error instead
        if (0 == copy_ret && !ARCH_MMU_ADDR_IS_USER(new_act->sa_action)) {
            copy_ret = EFAULT;
        }
        if (0 != copy_ret) {
            com_mm_slab_free(new_act, sizeof(*new_act));
            return COM_SYS_SYSCALL_ERR(copy_ret);
        }
    }

    com_proc_t      *curr_proc = ARCH_CPU_GET_THREAD()->proc;
    com_sigaction_t  old_act   = {.sa_action = SIG_DFL};
    com_sigaction_t *to_free   = NULL;
    kspinlock_acquire(&curr_proc->signal_lock);

    if (NULL != curr_proc->sigaction[sig]) {
        old_act = *curr_proc->sigaction[sig];
    }

    if (NULL != new_act) {
        to_free                   = curr_proc->sigaction[sig];
        curr_proc->sigaction[sig] = new_act;
    }

    kspinlock_release(&curr_proc->signal_lock);

    if (NULL != to_free) {
        com_mm_slab_free(to_free, sizeof(com_sigaction_t));
    }

    if (NULL != oact) {
        int copy_ret = kmemcpy_to_user(oact, &old_act, sizeof(old_act));
        if (0 != copy_ret) {
            return COM_SYS_SYSCALL_ERR(copy_ret);
        }
    }

    return COM_SYS_SYSCALL_OK(0);
}
