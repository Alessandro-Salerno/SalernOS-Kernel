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

#include <lib/mem.h>
#include <vendor/printf.h>
#include <lib/str.h>

#define memcpy(dst, src, sz) kmemcpy((dst), (src), (sz))
#define memset(dst, val, sz) kmemset((dst), (sz), (val))
#define memcmp(b1, b2, sz)   kmemcmp((b1), (b2), (sz))
#define memchr(s, c, n)      kmemchr((s), (c), (n))
#define memmove(...)         kmemmove(__VA_ARGS__)

#define strlen(s)      kstrlen((s))
#define strcmp(s1, s2) kstrcmp((s1), (s2))

#define FLANTERM_CUSTOM_CRT
