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
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/proc.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/sys/syscall.h>
#include <lib/usercopy.h>
#include <lib/util.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/wait.h>

static int waitpid_check(com_proc_t *curr_proc,
                         com_proc_t *towait,
                         int        *status,
                         bool       *reaped) {
    kspinlock_acquire(&towait->signal_lock);

    if (E_COM_PROC_STATE_EXITED == towait->state && towait->state_notified) {
        kspinlock_release(&towait->signal_lock);
        return -ECHILD;
    }

    if (E_COM_PROC_STATE_EXITED == towait->state) {
        *status = COM_IPC_SIGNAL_NONE == towait->state_cause_sig
                      ? (towait->exit_code & 0xff) << 8
                      : towait->state_cause_sig & 0x7f;
        towait->state_notified = true;
        __atomic_sub_fetch(&curr_proc->num_children, 1, __ATOMIC_RELAXED);
        kspinlock_release(&towait->signal_lock);
        *reaped = true;
        return towait->pid;
    }

    if (E_COM_PROC_STATE_STOPPED == towait->state && !towait->state_notified) {
        *status = 0x7F | (towait->state_cause_sig << 8);
        towait->state_notified = true;
        kspinlock_release(&towait->signal_lock);
        return towait->pid;
    }

    kspinlock_release(&towait->signal_lock);
    return 0;
}

static int
waitpid_proc(com_proc_t *curr_proc, pid_t pid, int *status, int flags) {
    while (true) {
        kspinlock_acquire(&curr_proc->waitpid_lock);

        com_proc_t *towait = -1 == pid
                                 ? com_sys_proc_get_arbitrary_child(curr_proc)
                                 : com_sys_proc_get_by_pid(pid);

        int  ret    = -ECHILD;
        bool reaped = false;
        bool slept  = false;
        if (NULL != towait &&
            0 != __atomic_load_n(&curr_proc->num_children, __ATOMIC_RELAXED) &&
            towait->parent_pid == curr_proc->pid) {
            ret = waitpid_check(curr_proc, towait, status, &reaped);
        }

        if (0 == ret && !(WNOHANG & flags)) {
            ret   = -kspinlock_wait(&curr_proc->waitpid_lock,
                                    &curr_proc->waitpid_waitlist,
                                    0,
                                    true);
            slept = true;
        }

        kspinlock_release(&curr_proc->waitpid_lock);

        if (NULL != towait) {
            if (reaped) {
                COM_SYS_PROC_RELEASE(towait);
            }
            COM_SYS_PROC_RELEASE(towait);
        }

        if (!slept || 0 != ret) {
            return ret;
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

    int kstatus = 0;
    ret         = waitpid_proc(curr_proc, pid, &kstatus, flags);
    if (ret > 0 && NULL != status &&
        0 != kmemcpy_to_user(status, &kstatus, sizeof(kstatus))) {
        ret = -EFAULT;
    }

end:
    if (ret < 0) {
        return COM_SYS_SYSCALL_ERR(-ret);
    }

    return COM_SYS_SYSCALL_OK(ret);
}
