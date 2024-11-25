/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2024 Alessandro Salerno

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

#include <stddef.h>
#include <stdint.h>

void   kmemset(void *buff, size_t buffsize, uint8_t val);
int8_t kmemcmp(void *buff1, void *buff2, size_t buffsize);

void kmemcpy(void *dst, void *src, size_t buffsize);
