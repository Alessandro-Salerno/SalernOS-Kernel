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

#include <errno.h>
#include <lib/util.h>
#include <stdbool.h>
#include <uacpi/uacpi.h>

#define UACPI_UTIL_LOG(...) KOPTMSG("UACPI", __VA_ARGS__)

KUSED static inline uacpi_status uacpi_util_posix_to_status(int e) {
    switch (e) {
        case 0:
            return UACPI_STATUS_OK;
        case ENOSYS:
        case EOPNOTSUPP:
#if ENOTSUP != EOPNOTSUPP
        case ENOTSUP:
#endif
            return UACPI_STATUS_UNIMPLEMENTED;
        case EINVAL:
            return UACPI_STATUS_INVALID_ARGUMENT;
        case ENOMEM:
            return UACPI_STATUS_OUT_OF_MEMORY;
        case EPERM:
        case EFAULT:
            return UACPI_STATUS_DENIED;
        default:
            KASSERT(!"unsupported error");
    }
}

KUSED static int uacpi_util_status_to_posix(uacpi_status status) {
    switch (status) {
        case UACPI_STATUS_OK:
            return 0;
        default:
            KDEBUG("wtf, got %zu", status);
            KASSERT(false);
    }
}
