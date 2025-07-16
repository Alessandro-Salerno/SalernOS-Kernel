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

#pragma once

#include <arch/context.h>
#include <arch/syscall.h>
#include <stdbool.h>
#include <stdint.h>

#define COM_SYS_SYSCALL(name)                                \
    com_syscall_ret_t name(arch_context_t    *__syscall_ctx, \
                           arch_syscall_arg_t __syscall_a1,  \
                           arch_syscall_arg_t __syscall_a2,  \
                           arch_syscall_arg_t __syscall_a3,  \
                           arch_syscall_arg_t __syscall_a4)
#define COM_SYS_SYSCALL_CONTEXT()        (__syscall_ctx)
#define COM_SYS_SYSCALL_ARG(type, num)   ((type)(__syscall_a##num))
#define COM_SYS_SYSCALL_UNUSED_CONTEXT() (void)(COM_SYS_SYSCALL_CONTEXT())
#define COM_SYS_SYSCALL_UNUSED_START(first_unused) \
    __COM_SYS_SYSCALL_UNUSED_##first_unused

#define __COM_SYS_SYSCALL_UNUSED_4 (void)(__syscall_a4)
#define __COM_SYS_SYSCALL_UNUSED_3 \
    __COM_SYS_SYSCALL_UNUSED_4;    \
    (void)(__syscall_a3)
#define __COM_SYS_SYSCALL_UNUSED_2 \
    __COM_SYS_SYSCALL_UNUSED_3;    \
    (void)(__syscall_a2)
#define __COM_SYS_SYSCALL_UNUSED_1 \
    __COM_SYS_SYSCALL_UNUSED_2;    \
    (void)(__syscall_a1)
#define __COM_SYS_SYSCALL_UNUSED_0    \
    COM_SYS_SYSCALL_UNUSED_CONTEXT(); \
    __COM_SYS_SYSCALL_UNUSED_1

#define COM_SYS_SYSCALL_OK(v)                      \
    (com_syscall_ret_t) {                          \
        .value = (v), .err = 0, .discarded = false \
    }
#define COM_SYS_SYSCALL_ERR(e)                      \
    (com_syscall_ret_t) {                           \
        .value = -1, .err = (e), .discarded = false \
    }
#define COM_SYS_SYSCALL_DISCARD() \
    (com_syscall_ret_t) {         \
        .discarded = true         \
    }

#define COM_SYS_SYSCALL_BASE_OK()  {0, 0, false};
#define COM_SYS_SYSCALL_BASE_ERR() {-1, 0, false}

typedef struct {
    uintmax_t value;
    uintmax_t err;
    bool discarded; // If true, ret.value and ret.err shall not be stored in
                    // return value registers by the arch-specific system call
                    // dispatcher
} com_syscall_ret_t;

typedef com_syscall_ret_t (*com_intf_syscall_t)(arch_context_t    *ctx,
                                                arch_syscall_arg_t a1,
                                                arch_syscall_arg_t a2,
                                                arch_syscall_arg_t a3,
                                                arch_syscall_arg_t a4);

void com_sys_syscall_register(uintmax_t number, com_intf_syscall_t handler);
void com_sys_syscall_init(void);

// SYSCALLS

COM_SYS_SYSCALL(com_sys_syscall_write);
COM_SYS_SYSCALL(com_sys_syscall_read);
COM_SYS_SYSCALL(com_sys_syscall_execve);
COM_SYS_SYSCALL(com_sys_syscall_fork);
COM_SYS_SYSCALL(com_sys_syscall_sysinfo);
COM_SYS_SYSCALL(com_sys_syscall_waitpid);
COM_SYS_SYSCALL(com_sys_syscall_exit);
COM_SYS_SYSCALL(com_sys_syscall_ioctl);
COM_SYS_SYSCALL(com_sys_syscall_open);
COM_SYS_SYSCALL(com_sys_syscall_mmap);
COM_SYS_SYSCALL(com_sys_syscall_seek);
COM_SYS_SYSCALL(com_sys_syscall_isatty);
COM_SYS_SYSCALL(com_sys_syscall_truncate);
COM_SYS_SYSCALL(com_sys_syscall_pipe);
COM_SYS_SYSCALL(com_sys_syscall_getpid);
COM_SYS_SYSCALL(com_sys_syscall_clone);
COM_SYS_SYSCALL(com_sys_syscall_exit_thread);
COM_SYS_SYSCALL(com_sys_syscall_futex);
COM_SYS_SYSCALL(com_sys_syscall_getppid);
COM_SYS_SYSCALL(com_sys_syscall_getpgid);
COM_SYS_SYSCALL(com_sys_syscall_getsid);
COM_SYS_SYSCALL(com_sys_syscall_setpgid);
COM_SYS_SYSCALL(com_sys_syscall_setsid);
COM_SYS_SYSCALL(com_sys_syscall_sigprocmask);
COM_SYS_SYSCALL(com_sys_syscall_sigpending);
COM_SYS_SYSCALL(com_sys_syscall_ssigthreadmask);
COM_SYS_SYSCALL(com_sys_syscall_sigaction);
COM_SYS_SYSCALL(com_sys_syscall_kill);
COM_SYS_SYSCALL(com_sys_syscall_kill_thread);
COM_SYS_SYSCALL(com_sys_syscall_sigreturn);
