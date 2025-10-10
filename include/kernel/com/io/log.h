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

#include <kernel/com/sys/panic.h>
#include <vendor/printf.h>
#include <lib/util.h>
#include <stddef.h>

struct com_vnode;
typedef void (*com_intf_log_t)(const char *s, size_t n);

void com_io_log_set_hook_nolock(com_intf_log_t hook);
void com_io_log_putc_nolock(char c);
void com_io_log_puts_nolock(const char *s);
void com_io_log_putsn_nolock(const char *s, size_t n);
void com_io_log_set_vnode_nolock(struct com_vnode *vnode);
void com_io_log_set_user_hook_nolock(com_intf_log_t hook);

void com_io_log_lock(void);
void com_io_log_unlock(void);

void com_io_log_set_hook(com_intf_log_t hook);
void com_io_log_putc(char c);
void com_io_log_puts(const char *s);
void com_io_log_putsn(const char *s, size_t n);
void com_io_log_set_vnode(struct com_vnode *vnode);
void com_io_log_set_user_hook(com_intf_log_t hook);
