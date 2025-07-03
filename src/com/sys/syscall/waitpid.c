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
#include <stdint.h>

// TODO: handle case in which pid < 0 and proc groups
// TODO: do stuff with signals and flags
com_syscall_ret_t com_sys_syscall_waitpid(arch_context_t *ctx,
                                          uintmax_t       pid,
                                          uintmax_t       statusptr,
                                          uintmax_t       flags,
                                          uintmax_t       unused) {
    (void)ctx;
    (void)unused;
    com_proc_t       *curr   = hdr_arch_cpu_get_thread()->proc;
    com_proc_t       *towait = com_sys_proc_get_by_pid(pid);
    com_syscall_ret_t ret    = {0, 0};
    int              *status = (int *)statusptr;

    com_sys_proc_acquire_glock();

    if (0 == curr->num_children || NULL == towait ||
        towait->parent_pid != curr->pid) {
        ret.err = ECHILD;
        goto cleanup;
    }

    while (1) {
        if (towait->exited) {
            *status   = towait->exit_status;
            ret.value = pid;
            com_sys_proc_destroy(towait);
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
