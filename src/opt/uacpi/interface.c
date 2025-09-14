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

| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#include <kernel/opt/uacpi.h>
#include <lib/util.h>
#include <uacpi/uacpi.h>

static int uacpi_to_posix(uacpi_status status) {
    switch (status) {
        case UACPI_STATUS_OK:
            return 0;
        default:
            KASSERT(!"something went wrong");
    }
}

int opt_uacpi_init(opt_uacpi_u64_t flags) {
    return uacpi_to_posix(uacpi_initialize(flags));
}
