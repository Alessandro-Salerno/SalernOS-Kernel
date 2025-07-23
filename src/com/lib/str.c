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

#include <lib/mem.h>
#include <lib/str.h>
#include <stddef.h>
#include <stdint.h>

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

const char *kuitoa(uint64_t val, char *s) {
    uint64_t size_test = val;
    uint8_t  size      = 0;
    uint8_t  idx       = 0;

    while (size_test >= 10) {
        size_test /= 10;
        size++;
    }

    while (idx < size) {
        uint8_t rem = val % 10;
        val /= 10;
        s[size - idx] = rem + '0';
        idx++;
    }

    uint8_t rem = val % 10;
    val /= 10;
    s[size - idx] = rem + '0';
    s[size + 1]   = 0;

    return s;
}

const char *kitoa(int64_t val, char *s) {
    uint64_t size_test   = 10;
    uint8_t  size        = 0;
    uint8_t  idx         = 0;
    uint8_t  is_negative = 0;

    is_negative = val < 0;
    s[0]        = (val < 0) ? '-' : '+';

    if (val < 0) {
        val *= -1;
    }

    while (val / size_test > 0) {
        size_test *= 10;
        size++;
    }

    while (idx < size) {
        uint8_t rem = val % 10;
        val /= 10;
        s[is_negative + size - idx] = rem + '0';
        idx++;
    }

    uint8_t rem = val % 10;
    val /= 10;
    s[is_negative + size - idx] = rem + '0';
    s[is_negative + size + 1]   = 0;

    return s;
}

const char *kxuitoa(uint64_t val, char *s) {
    uint64_t size_test = val;
    uint8_t  size      = 0;
    uint8_t  idx       = 0;

    while (size_test >= 16) {
        size_test /= 16;
        size++;
    }

    uint64_t diff = 16 - size;

    for (uint64_t i = 0; i < diff; i++) {
        s[i] = '0';
    }

    while (idx < size) {
        uint8_t rem = val % 16;
        val /= 16;
        if (rem < 10) {
            s[15 - idx] = rem + '0';
        } else {
            s[15 - idx] = rem - 10 + 'A';
        }
        idx++;
    }

    uint8_t rem = val % 16;
    val /= 16;
    if (rem < 10) {
        s[15 - idx] = rem + '0';
    } else {
        s[15 - idx] = rem - 10 + 'A';
    }
    s[16] = 0;

    return s;
}

// TODO: adopt this everywhere I calculate the seocnd-last index or similar
void kstrpathpenult(const char *s,
                    size_t      len,
                    size_t     *penult_len,
                    size_t     *end_idx,
                    size_t     *end_len) {
    while (len > 0 && '/' == s[len - 1]) {
        len--;
    }

    char *end = kmemrchr(s, '/', len);

    size_t plen = 0;
    size_t eidx = 0;
    size_t elen = len;

    if (NULL != end) {
        plen = end - s;
        eidx = plen + 1;
        elen = len - eidx;
    }

    if (NULL != penult_len) {
        *penult_len = plen;
    }

    if (NULL != end_idx) {
        *end_idx = eidx;
    }

    if (NULL != end_len) {
        *end_len = elen;
    }
}
