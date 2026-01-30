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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int    kstrcmp(const char *s1, const char *s2);
size_t kstrlen(const char *s);
void   kstrcpy(char *dst, const char *src);

const char *kuitoa(uint64_t val, char *s);
const char *kitoa(int64_t val, char *s);
const char *kxuitoa(uint64_t val, char *s, bool have_leading);
void        kstrpathpenult(const char *s,
                           size_t      len,
                           size_t     *penult_len,
                           size_t     *end_idx,
                           size_t     *end_len);
