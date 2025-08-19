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
#include <lib/util.h>

// SYSCALL: setpgid(pid_t pid, pid_t pgid)
COM_SYS_SYSCALL(com_sys_syscall_setpgid) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(3);

    pid_t pid  = COM_SYS_SYSCALL_ARG(pid_t, 1);
    pid_t pgid = COM_SYS_SYSCALL_ARG(pid_t, 2);

    if (pgid < 0 || pid < 0) {
        return COM_SYS_SYSCALL_ERR(EINVAL);
    }

    com_syscall_ret_t ret       = COM_SYS_SYSCALL_BASE_ERR();
    com_proc_t       *curr_proc = ARCH_CPU_GET_THREAD()->proc;

    com_proc_t *proc = curr_proc;
    KASSERT(NULL != proc);
    com_spinlock_acquire(&proc->pg_lock);
    KASSERT(NULL != proc->proc_group);

    if (0 == pgid) {
        pgid = proc->pid;
    }

    if (pgid == proc->proc_group->pgid) {
        ret.value = 0;
        goto end;
    }

    KASSERT(NULL != proc->proc_group->session);
    if (proc->pid == proc->proc_group->session->sid) {
        ret.err = EPERM;
        goto end;
    }

    if (0 != pid) {
        proc = com_sys_proc_get_by_pid(pid);
        if (NULL == proc || (proc->pid != curr_proc->pid &&
                             proc->parent_pid != curr_proc->pid)) {
            ret.err = ESRCH;
            goto end;
        }
    }

    if (proc->did_execve) {
        ret.err = EACCES;
        goto end;
    }

    // Doing a query on the process group hashmap is actually thread safe even
    // if the lock is acquired only inside the function. This is because we know
    // (hopefully) that a process group may only be created by setpgid and
    // setsid, which first need to acquire the locks above (proc->pg_lock)
    com_proc_group_t *group = com_sys_proc_get_group_by_pgid(pgid);
    if (NULL == group) {
        group = com_sys_proc_new_group(proc, NULL);
    }
    com_sys_proc_join_group_nolock(proc, group);

    ret.value = 0;

end:
    com_spinlock_release(&proc->pg_lock);
    return ret;
}
