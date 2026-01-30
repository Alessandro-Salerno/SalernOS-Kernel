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
#include <errno.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/syscall.h>
#include <lib/util.h>

// SYSCALL: getpgid(pid_t pid)
COM_SYS_SYSCALL(com_sys_syscall_getpgid) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    pid_t pid = COM_SYS_SYSCALL_ARG(pid_t, 1);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();
    com_proc_t   *curr_proc   = curr_thread->proc;

    if (0 == pid) {
        kspinlock_acquire(&curr_proc->pg_lock);
        KASSERT(NULL != curr_proc->proc_group);
        pid_t pgid = curr_proc->proc_group->pgid;
        KDEBUG("pid=%d has pgid=%d", curr_proc->pid, pgid)
        kspinlock_release(&curr_proc->pg_lock);
        return COM_SYS_SYSCALL_OK(pgid);
    }

    if (pid < 1) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    com_proc_t *proc = com_sys_proc_get_by_pid(pid);

    if (NULL == proc) {
        return COM_SYS_SYSCALL_ERR(ESRCH);
    }

    com_syscall_ret_t ret = COM_SYS_SYSCALL_BASE_ERR();

    KASSERT(NULL != curr_proc->proc_group);
    KASSERT(NULL != proc->proc_group);

    kspinlock_acquire(&curr_proc->pg_lock);
    if (proc != curr_proc) {
        kspinlock_acquire(&proc->pg_lock);
    }

    if (proc->proc_group->session->sid != curr_proc->proc_group->session->sid) {
        ret.err = EPERM;
        goto end;
    }

    ret.value = proc->proc_group->pgid;

end:
    if (proc != curr_proc) {
        kspinlock_release(&proc->pg_lock);
    }
    kspinlock_release(&curr_proc->pg_lock);
    return ret;
}
