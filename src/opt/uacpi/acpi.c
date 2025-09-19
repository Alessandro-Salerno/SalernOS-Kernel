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

#include <kernel/opt/acpi.h>
#include <uacpi/tables.h>
#include <uacpi/uacpi.h>

#include "uacpi_util.h"

int opt_acpi_impl_find_table_by_signature(opt_acpi_table_t *out,
                                          const char       *signature) {
    uacpi_table  tbl;
    uacpi_status ret = uacpi_table_find_by_signature(signature, &tbl);
    if (UACPI_STATUS_OK != ret) {
        goto end;
    }

    out->phys_addr = tbl.ptr;
    out->virt_addr = (void *)tbl.virt_addr;
end:
    return uacpi_util_status_to_posix(ret);
}
