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

#include <stddef.h>
#include <stdint.h>

void *com_mm_pmm_alloc(void);
void *com_mm_pmm_alloc_many(size_t pages);
void  com_mm_pmm_free(void *page);
void  com_mm_pmm_free_many(void *base, size_t pages);
void  com_mm_pmm_get_info(uintmax_t *used_mem,
                          uintmax_t *free_mem,
                          uintmax_t *reserved_mem,
                          uintmax_t *sys_mem,
                          uintmax_t *mem_size);
void  com_mm_pmm_init(void);
