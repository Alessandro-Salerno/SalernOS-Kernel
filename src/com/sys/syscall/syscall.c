/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2024 Alessandro Salerno                           |
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
#include <kernel/com/log.h>
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
  com_log_puts((const char *)arg1);

  return (com_syscall_ret_t){0, 0};
}

void com_sys_syscall_register(uintmax_t number, com_intf_syscall_t handler) {
  ASSERT(number < MAX_SYSCALLS);
  ASSERT(NULL == Com_Sys_Syscall_Table[number]);

  DEBUG(
      "registering handler at %x with number %u (%x)", handler, number, number);
  Com_Sys_Syscall_Table[number] = handler;
}

void com_sys_syscall_init(void) {
  LOG("initializing common system calls");
  com_sys_syscall_register(0x00, test_syscall);
  com_sys_interrupt_register(0x80, arch_syscall_handle, NULL);
}
