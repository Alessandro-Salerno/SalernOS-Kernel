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

#include <lib/str.h>

int kstrcmp(const char *s1, const char *s2) {
    // NOTE: this or is correct because if just one of the two is zero (thus one
    // string is shorter), then the if will pass and the function will return -1
    while (0 != *s1 || 0 != *s2) {
        if (*s1 != *s2) {
            return -1;
        }

        s1++;
        s2++;
    }

    return 0;
}

size_t kstrlen(const char *s) {
    size_t n = 0;
    for (; 0 != *s; s++) {
        n++;
    }
    return n;
}

void kstrcpy(char *dst, const char *src) {
    size_t n = 0;
    for (; 0 != src[n]; n++) {
        dst[n] = src[n];
    }
    dst[n] = 0;
}
