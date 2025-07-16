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
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <lib/util.h>
#include <stdatomic.h>
#include <stdint.h>

// TODO: handle case in which pid < 0 and proc groups
// TODO: do stuff with signals and flags
// SYSCALL: waitpid(pid_t pid, int *status, int flags)
COM_SYS_SYSCALL(com_sys_syscall_waitpid) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    pid_t pid    = COM_SYS_SYSCALL_ARG(pid_t, 1);
    int  *status = COM_SYS_SYSCALL_ARG(int *, 2);
    int   flags  = COM_SYS_SYSCALL_ARG(int, 3);
    KASSERT(pid > 0);

    com_proc_t       *curr   = ARCH_CPU_GET_THREAD()->proc;
    com_proc_t       *towait = com_sys_proc_get_by_pid(pid);
    com_syscall_ret_t ret    = COM_SYS_SYSCALL_BASE_OK();

    com_sys_proc_acquire_glock();

    if (0 == __atomic_load_n(&curr->num_children, __ATOMIC_RELAXED) ||
        NULL == towait || towait->parent_pid != curr->pid) {
        ret.err = ECHILD;
        goto cleanup;
    }

    while (true) {
        if (towait->exited) {
            *status   = towait->exit_status;
            ret.value = pid;
            __atomic_add_fetch(&towait->num_ref, -1, __ATOMIC_SEQ_CST);
            goto cleanup;
        }

        (void)flags;
        // TODO: handle flags

        com_sys_proc_wait(curr);
    }

cleanup:
    com_sys_proc_release_glock();
    return ret;
}
