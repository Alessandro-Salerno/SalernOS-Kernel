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

static com_syscall_ret_t test_syscall(arch_context_t *ctx,
                                      uintmax_t       arg1,
                                      uintmax_t       arg2,
                                      uintmax_t       arg3,
                                      uintmax_t       arg4) {
    (void)ctx;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    com_io_log_puts((const char *)arg1);

    return (com_syscall_ret_t){0, 0};
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

    com_sys_interrupt_register(0x80, arch_syscall_handle, NULL);
}
