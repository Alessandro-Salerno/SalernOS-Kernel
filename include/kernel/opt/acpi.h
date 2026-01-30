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

#include <arch/info.h>

#ifndef HAVE_OPT_ACPI
#error "cannot include this header without having opt/acpi"
#endif

#ifndef OPT_ACPI_IMPLEMENTATION
#error "cannot use opt/acpi without having an acpi implementation"
#endif

typedef struct opt_acpi_table {
    void *phys_addr;
    void *virt_addr;
} opt_acpi_table_t;

// these functions are implemennted by platforms using this option
arch_rsdp_t *arch_info_get_rsdp(void);

// these functions are implemented by the option and used by platformes
int opt_acpi_impl_find_table_by_signature(opt_acpi_table_t *out,
                                          const char       *signature);

// these functions are implemented by the ACPI option itself
int opt_acpi_init_pci(void);
