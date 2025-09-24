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

#define ARCH_LIBHELP_FAST_MEMSET(dst, c, n)   \
    asm volatile("cld; rep stosb"             \
                 : "=c"((int){0})             \
                 : "rdi"(dst), "a"(c), "c"(n) \
                 : "flags", "memory", "rdi");

#define ARCH_LIBHELP_FAST_MEMCPY(dst, src, n) \
    asm volatile("cld; rep movsb"             \
                 : "=c"((int){0})             \
                 : "D"(dst), "S"(src), "c"(n) \
                 : "flags", "memory");

#define ARCH_LIBHELP_FAST_REVERSE_MEMCPY(dst, src, n) \
    asm volatile("std; rep movsb; cld;"               \
                 : "=c"((int){0})                     \
                 : "D"(dst), "S"(src), "c"(n)         \
                 : "flags", "memory");
