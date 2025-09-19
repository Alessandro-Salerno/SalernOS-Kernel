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
#include <uacpi/event.h>
#include <uacpi/uacpi.h>

#include "uacpi_util.h"

int opt_uacpi_init(opt_uacpi_u64_t flags) {
    KLOG("initializing uacpi");

    uacpi_status ret = uacpi_initialize(flags);
    if (KUNKLIKELY(UACPI_STATUS_OK != ret)) {
        goto end;
    }

    int kernel_ret = arch_uacpi_early_init();
    if (0 != kernel_ret) {
        return kernel_ret;
    }

    ret = uacpi_namespace_load();
    if (KUNKLIKELY(UACPI_STATUS_OK != ret)) {
        goto end;
    }

    ret = uacpi_namespace_initialize();
    if (KUNKLIKELY(UACPI_STATUS_OK != ret)) {
        goto end;
    }

    ret = uacpi_finalize_gpe_initialization();

end:
    return uacpi_util_status_to_posix(ret);
}
