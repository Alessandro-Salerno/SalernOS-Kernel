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

#include <arch/context.h>
#include <arch/cpu.h>
#include <kernel/com/io/log.h>
#include <kernel/com/sys/interrupt.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/platform/syscall.h>
#include <stdint.h>

#define MAX_SYSCALLS 512

volatile com_intf_syscall_t Com_Sys_Syscall_Table[MAX_SYSCALLS] = {0};

// test_syscall(const char *s)
static COM_SYS_SYSCALL(test_syscall) {
#ifndef DISABLE_LOGGING
    COM_SYS_SYSCALL_UNUSED_CONTEXT();
    COM_SYS_SYSCALL_UNUSED_START(2);

    const char *s = COM_SYS_SYSCALL_ARG(const char *, 1);

    com_io_log_acquire();
    com_io_log_puts(s);
    com_io_log_release();
#else
    COM_SYS_SYSCALL_UNUSED_START(0);
#endif

    return COM_SYS_SYSCALL_OK(0);
}

void com_sys_syscall_register(uintmax_t number, com_intf_syscall_t handler) {
    KASSERT(number < MAX_SYSCALLS);
    KASSERT(NULL == Com_Sys_Syscall_Table[number]);

    KDEBUG("registering handler at %x with number %u (%x)",
           handler,
           number,
           number);
    Com_Sys_Syscall_Table[number] = handler;
}

void com_sys_syscall_init(void) {
    KLOG("initializing common system calls");
    com_sys_syscall_register(0x00, test_syscall);
    com_sys_syscall_register(0x01, com_sys_syscall_write);
    com_sys_syscall_register(0x02, com_sys_syscall_read);
    com_sys_syscall_register(0x03, com_sys_syscall_execve);
    com_sys_syscall_register(0x04, com_sys_syscall_fork);
    com_sys_syscall_register(0x05, com_sys_syscall_sysinfo);
    com_sys_syscall_register(0x06, com_sys_syscall_waitpid);
    com_sys_syscall_register(0x07, com_sys_syscall_exit);
    com_sys_syscall_register(0x08, com_sys_syscall_ioctl);
    com_sys_syscall_register(0x09, com_sys_syscall_open);
    com_sys_syscall_register(0x0A, com_sys_syscall_mmap);
    com_sys_syscall_register(0x0B, arch_syscall_set_tls);
    com_sys_syscall_register(0x0C, com_sys_syscall_seek);
    com_sys_syscall_register(0x0D, com_sys_syscall_isatty);
    // TODO: add stat syscall with code 0x0E
    com_sys_syscall_register(0x0F, com_sys_syscall_truncate);
    com_sys_syscall_register(0x10, com_sys_syscall_pipe);
    com_sys_syscall_register(0x11, com_sys_syscall_getpid);
    com_sys_syscall_register(0x12, com_sys_syscall_clone);
    com_sys_syscall_register(0x13, com_sys_syscall_exit_thread);
    com_sys_syscall_register(0x14, com_sys_syscall_futex);
    com_sys_syscall_register(0x15, com_sys_syscall_getppid);
    com_sys_syscall_register(0x16, com_sys_syscall_getpgid);
    com_sys_syscall_register(0x17, com_sys_syscall_getsid);
    com_sys_syscall_register(0x18, com_sys_syscall_setpgid);
    com_sys_syscall_register(0x19, com_sys_syscall_setsid);
    com_sys_syscall_register(0x1A, com_sys_syscall_sigprocmask);
    com_sys_syscall_register(0x1B, com_sys_syscall_sigpending);
    com_sys_syscall_register(0x1C, com_sys_syscall_ssigthreadmask);
    com_sys_syscall_register(0x1D, com_sys_syscall_sigaction);
    com_sys_syscall_register(0x1E, com_sys_syscall_kill);
    com_sys_syscall_register(0x1F, com_sys_syscall_kill_thread);
    com_sys_syscall_register(0x20, com_sys_syscall_sigreturn);

    com_sys_interrupt_register(0x80, arch_syscall_handle, NULL);
}
