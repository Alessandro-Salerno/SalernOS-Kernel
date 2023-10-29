/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2023 Alessandro Salerno

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**********************************************************************/

#pragma once

#include <kerntypes.h>

#include "mm/bmp.h"
#include "mm/mmap.h"

void pgfa_initialize();
void pgfa_free(void *__address, uint64_t __pagecount);
void pgfa_lock(void *__address, uint64_t __pagecount);
void pgfa_unreserve(void *__address, uint64_t __pagecount);
void pgfa_reserve(void *__address, uint64_t __pagecount);

void  pgfa_info_get(uint64_t *__freemem,
                    uint64_t *__usedmem,
                    uint64_t *__reservedmem);
void *pgfa_page_new();
