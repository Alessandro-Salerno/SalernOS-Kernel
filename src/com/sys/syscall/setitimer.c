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
#include <kernel/com/ipc/signal.h>
#include <kernel/com/sys/callout.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/sys/thread.h>
#include <lib/util.h>
#include <stdint.h>
#include <sys/time.h>

static void itimer_callout(com_callout_t *callout) {
    com_thread_t     *thread    = callout->arg;
    struct itimerval *itimerval = &thread->real_timer.itimerval;

    if (0 == itimerval->it_value.tv_sec && 0 == itimerval->it_value.tv_usec) {
        return;
    }

    uintmax_t expiry = itimerval->it_value.tv_sec * KNANOS_PER_SEC +
                       itimerval->it_value.tv_usec * 1000UL +
                       thread->real_timer.ctime;

    if (com_sys_callout_get_time() >= expiry) {
        if (0 != itimerval->it_interval.tv_sec ||
            0 != itimerval->it_interval.tv_usec) {
            itimerval->it_value = itimerval->it_interval;
            uintmax_t delay     = itimerval->it_value.tv_sec * KNANOS_PER_SEC +
                              itimerval->it_value.tv_usec * 1000UL;
            com_sys_callout_reschedule(callout, delay);
        }

        com_ipc_signal_send_to_thread(thread, SIGALRM, thread->proc);
    }
}

// SYSCALL: setitimer(int which, struct itimerval *new_val, struct itimerval
// *old_val)
COM_SYS_SYSCALL(com_sys_syscall_setitimer) {
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(4);

    int               which   = COM_SYS_SYSCALL_ARG(int, 1);
    struct itimerval *new_val = COM_SYS_SYSCALL_ARG(void *, 2);
    struct itimerval *old_val = COM_SYS_SYSCALL_ARG(void *, 3);

    com_thread_t *curr_thread = ARCH_CPU_GET_THREAD();

    // TODO: support other timer types
    KASSERT(ITIMER_REAL == which);

    if (NULL != old_val) {
        *old_val = curr_thread->real_timer.itimerval;
    }

    if (NULL != new_val) {
        if (new_val->it_value.tv_sec < 0 || new_val->it_value.tv_usec < 0) {
            return COM_SYS_SYSCALL_ERR(EINVAL);
        }

        curr_thread->real_timer.ctime     = com_sys_callout_get_time();
        curr_thread->real_timer.itimerval = *new_val;
        uintmax_t delay = new_val->it_value.tv_sec * KNANOS_PER_SEC +
                          new_val->it_value.tv_usec * 1000UL;

        if (0 != delay) {
            com_sys_callout_add(itimer_callout, curr_thread, delay);
        }
    }

    return COM_SYS_SYSCALL_OK(0);
}
