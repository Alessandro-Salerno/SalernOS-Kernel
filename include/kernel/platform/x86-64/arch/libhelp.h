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

#define ARCH_LIBHELP_FAST_MEMSET(dst, c, n) \
    asm volatile("cld; rep stosb" : "+D"(dst), "+c"(n) : "a"(val) : "memory");

#define ARCH_LIBHELP_FAST_MEMCPY(dst, src, n)          \
    do {                                               \
        void       *__d = (dst);                       \
        const void *__s = (src);                       \
        size_t      __n = (n);                         \
        asm volatile("cld; rep movsb"                  \
                     : "+D"(__d), "+S"(__s), "+c"(__n) \
                     :                                 \
                     : "flags", "memory");             \
    } while (0)

#define ARCH_LIBHELP_FAST_REVERSE_MEMCPY(dst, src, n)             \
    do {                                                          \
        size_t      __n = (n);                                    \
        void       *__d = (unsigned char *)(dst) + __n - 1;       \
        const void *__s = (const unsigned char *)(src) + __n - 1; \
        asm volatile("std; rep movsb; cld"                        \
                     : "+D"(__d), "+S"(__s), "+c"(__n)            \
                     :                                            \
                     : "flags", "memory");                        \
    } while (0)
