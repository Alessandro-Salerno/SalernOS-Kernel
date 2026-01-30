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

// SYSCALL: setsid(void)
COM_SYS_SYSCALL(com_sys_syscall_setsid) {
    COM_SYS_SYSCALL_UNUSED_START(0);

    com_proc_t *curr_proc = ARCH_CPU_GET_THREAD()->proc;
    kspinlock_acquire(&curr_proc->pg_lock);
    KASSERT(NULL != curr_proc->proc_group);

    com_syscall_ret_t ret = COM_SYS_SYSCALL_BASE_ERR();

    if (curr_proc->proc_group->pgid == curr_proc->pid) {
        ret.err = EPERM;
        goto end;
    }

    com_sys_proc_new_session_nolock(curr_proc, NULL);
    ret.value = curr_proc->proc_group->pgid;

end:
    kspinlock_release(&curr_proc->pg_lock);
    return ret;
}
