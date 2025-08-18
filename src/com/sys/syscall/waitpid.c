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
#include <kernel/com/sys/signal.h>
#include <kernel/com/sys/syscall.h>
#include <lib/util.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/wait.h>

static int waitpid_proc(com_proc_t *curr_proc,
                        com_proc_t *towait,
                        int        *status,
                        int         flags) {
    if (NULL == towait ||
        0 == __atomic_load_n(&curr_proc->num_children, __ATOMIC_RELAXED) ||
        NULL == towait || towait->parent_pid != curr_proc->pid) {
        return -ECHILD;
    }

    while (true) {
        if (towait->exited) {
            *status = (towait->exit_status & 0xff) << 8;
            int ret = towait->pid;
            // TODO: i think this creates issues with multithreading (race where
            // one of two wating threads may read from deallocated memory)
            __atomic_add_fetch(&towait->num_ref, -1, __ATOMIC_SEQ_CST);
            return ret;
        }

        if (COM_SYS_SIGNAL_NONE != towait->stop_signal) {
            if (towait->stop_notified) {
                // KASSERT(WNOHANG & flags);
                return 0;
            }

            *status               = 0x7F | (towait->stop_signal << 8);
            towait->stop_notified = true;
            return towait->pid;
        }

        (void)flags;
        // TODO: handle flags

        com_sys_proc_wait(curr_proc);

        if (COM_SYS_SIGNAL_NONE != com_sys_signal_check()) {
            return -EINTR;
        }
    }
}

static int waitpid_group(com_proc_t *curr_proc, int pgid, int flags) {
    // TODO: implement this
    (void)curr_proc;
    (void)pgid;
    (void)flags;
    return -ENOSYS;
}

// SYSCALL: waitpid(pid_t pid, int *status, int flags)
COM_SYS_SYSCALL(com_sys_syscall_waitpid) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    pid_t pid    = COM_SYS_SYSCALL_ARG(pid_t, 1);
    int  *status = COM_SYS_SYSCALL_ARG(int *, 2);
    int   flags  = COM_SYS_SYSCALL_ARG(int, 3);

    com_proc_t *curr_proc = ARCH_CPU_GET_THREAD()->proc;
    int         ret       = 0; // 0 is NOT valid

    if (pid < -1) {
        ret = waitpid_group(curr_proc, -pid, flags);
        goto end;
    }

    com_sys_proc_acquire_glock();
    com_proc_t *towait = NULL;

    if (-1 == pid) {
        towait = com_sys_proc_get_arbitray_child(curr_proc);
    } else {
        towait = com_sys_proc_get_by_pid(pid);
    }

    ret = waitpid_proc(curr_proc, towait, status, flags);
    com_sys_proc_release_glock();

end:
    if (ret < 0) {
        return COM_SYS_SYSCALL_ERR(-ret);
    }

    return COM_SYS_SYSCALL_OK(ret);
}
