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

#include <stdint.h>

#define COM_SYS_SIGNAL_SIGMASK_INIT(maskptr)       *(maskptr) = 0UL;
#define COM_SYS_SIGNAL_SIGMASK_SET(maskptr, sig)   *(maskptr) |= (1 << (sig))
#define COM_SYS_SIGNAL_SIGMASK_UNSET(maskptr, sig) *(maskptr) &= ~(1 << (sig))
#define COM_SYS_SIGNAL_SIGMASK_ISSET(mask, sig)    (mask) & (1 << (sig))

typedef struct {
    unsigned long sig[1024 / (8 * sizeof(long))];
} com_sigset_t;

// Sigmask is essentially an entry into the sig array above
// The struct above is kept for compatibility with Linux (and mlibc currently),
// but there's no need for all those signals.
// So, to save some space in proc and thread structs, we use sigmask and only
// ever access sigset_t.sig[0]
typedef unsigned long com_sigmask_t;

void com_sys_signal_sigset_emptY(com_sigset_t *set);
