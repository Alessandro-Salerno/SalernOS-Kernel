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

#pragma once

#include <arch/context.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// NOTE: since this kernel's architecture is old and bad, some functions are
// expected to handle both user addresses and kernel addresses. The *_maybe_user
// variants exist for this very reason: they skip checking if the address is in
// the user range, and instead jump straight into the fault-tollerant copy. If
// the address is in the kernel range and a fault occurs, it is treated as a
// normal kernel fault (i.e., it panics); otherwise it is treated as a user
// fault and returns EFAULT

int kmemcpy_maybe_user(void *dst, const void *src, size_t buffsize);
int kmemmove_maybe_user(void *dst, void *src, size_t n);
int kmemset_maybe_user(void *buff, size_t buffsize, uint8_t val);

int kmemcpy_to_user(void *dst, const void *src, size_t buffsize);
int kmemcpy_from_user(void *dst, const void *src, size_t buffsize);
int kmemset_user(void *buff, size_t buffsize, uint8_t val);

// Called by the fault handler: resumes the running user copy with EFAULT
bool kusercopy_fixup(arch_context_t *ctx);
