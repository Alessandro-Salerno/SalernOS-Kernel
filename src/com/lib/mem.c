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
#include <stddef.h>
#include <stdint.h>

void kmemset(void *buff, size_t buffsize, uint8_t val) {
  // Use compiler optiimzations for better performance
  for (uint64_t i = 0; i < buffsize; i++)
    *(uint8_t *)((uint64_t)(buff) + i) = val;
}

int8_t kmemcmp(const void *buff1, const void *buff2, size_t buffsize) {
  for (uint64_t i = 0; i < buffsize; i++) {
    uint8_t buff1val = *(uint8_t *)(buff1 + i);
    uint8_t buff2val = *(uint8_t *)(buff2 + i);

    if (buff1val != buff2val) {
      return -1;
    }
  }

  return 0;
}

void kmemcpy(void *dst, const void *src, size_t buffsize) {
  uint64_t src_off = (uint64_t)src;
  uint64_t dst_off = (uint64_t)dst;

  for (uint64_t i = 0; i < buffsize; i++) {
    uint8_t val               = *(uint8_t *)(src_off + i);
    *(uint8_t *)(dst_off + i) = val;
  }
}

void *kmemchr(const void *str, int c, size_t n) {
  const char *s = str;
  for (size_t i = 0; i < n; i++) {
    if (c == s[i]) {
      return (void *)&s[i];
    }
  }

  return NULL;
}

void *kmemmove(void *dst, void *src, size_t n) {
  uint8_t       *pdest = (uint8_t *)dst;
  const uint8_t *psrc  = (const uint8_t *)src;

  if (src > dst) {
    for (size_t i = 0; i < n; i++) {
      pdest[i] = psrc[i];
    }
  } else if (src < dst) {
    for (size_t i = n; i > 0; i--) {
      pdest[i - 1] = psrc[i - 1];
    }
  }

  return dst;
}
