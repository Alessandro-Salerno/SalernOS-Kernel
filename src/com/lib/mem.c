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

#include <arch/libhelp.h>
#include <kernel/com/sys/profiler.h>
#include <lib/mem.h>
#include <stddef.h>
#include <stdint.h>

void *kmemset(void *buff, size_t buffsize, uint8_t val) {
    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_KMEMSET);
#ifdef ARCH_LIBHELP_FAST_MEMSET
    void  *dst = buff;
    size_t n   = buffsize;
    ARCH_LIBHELP_FAST_MEMSET(dst, val, n);
#else
    // Use compiler optiimzations for better performance
    for (uint64_t i = 0; i < buffsize; i++) {
        *(uint8_t *)((uint64_t)(buff) + i) = val;
    }
#endif
    com_sys_profiler_end_function(&profiler_data);
    return buff;
}

int8_t kmemcmp(const void *buff1, const void *buff2, size_t buffsize) {
    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_KMEMCMP);
    for (uint64_t i = 0; i < buffsize; i++) {
        uint8_t buff1val = *(uint8_t *)(buff1 + i);
        uint8_t buff2val = *(uint8_t *)(buff2 + i);

        if (buff1val != buff2val) {
            com_sys_profiler_end_function(&profiler_data);
            return -1;
        }
    }

    com_sys_profiler_end_function(&profiler_data);
    return 0;
}

void *kmemcpy(void *dst, const void *src, size_t buffsize) {
    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_KMEMCPY);
#ifdef ARCH_LIBHELP_FAST_MEMCPY
    ARCH_LIBHELP_FAST_MEMCPY(dst, src, buffsize);
#else
    uint64_t src_off = (uint64_t)src;
    uint64_t dst_off = (uint64_t)dst;

    for (uint64_t i = 0; i < buffsize; i++) {
        uint8_t val               = *(uint8_t *)(src_off + i);
        *(uint8_t *)(dst_off + i) = val;
    }
#endif
    com_sys_profiler_end_function(&profiler_data);
    return dst;
}

void *kmemchr(const void *str, int c, size_t n) {
    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_KMEMCHR);
    const unsigned char *s = str;
    for (size_t i = 0; i < n; i++) {
        if (c == s[i]) {
            com_sys_profiler_end_function(&profiler_data);
            return (void *)&s[i];
        }
    }

    com_sys_profiler_end_function(&profiler_data);
    return NULL;
}

void *kmemrchr(const void *str, int c, size_t n) {
    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_KMEMRCHR);
    const unsigned char *s = str;
    c                      = (unsigned char)c;

    while (n--) {
        if (s[n] == c) {
            com_sys_profiler_end_function(&profiler_data);
            return (void *)(s + n);
        }
    }

    com_sys_profiler_end_function(&profiler_data);
    return NULL;
}

void *kmemmove(void *dst, void *src, size_t n) {
    com_profiler_data_t profiler_data = com_sys_profiler_start_function(
        E_COM_PROFILE_FUNC_KMEMMOVE);
    if (src > dst) {
        kmemcpy(dst, src, n);
    } else if (src < dst) {
#ifdef ARCH_LIBHELP_FAST_REVERSE_MEMCPY
        ARCH_LIBHELP_FAST_REVERSE_MEMCPY(dst, src, n);
#else
        uint8_t       *pdest = (uint8_t *)dst;
        const uint8_t *psrc  = (const uint8_t *)src;
        for (size_t i = n; i > 0; i--) {
            pdest[i - 1] = psrc[i - 1];
        }
#endif
    }

    com_sys_profiler_end_function(&profiler_data);
    return dst;
}

void *kmemrepcpy(void *dst, const void *src, size_t buffsize, size_t rep) {
    if (0 == rep || 0 == buffsize) {
        return NULL;
    }

    kmemcpy(dst, src, buffsize);
    size_t copied   = buffsize;
    size_t dst_size = buffsize * rep;

    while (copied < dst_size) {
        size_t to_copy = copied;
        if (copied + to_copy > dst_size) {
            to_copy = dst_size - copied;
        }
        kmemcpy((uint8_t *)dst + copied, dst, to_copy);
        copied += to_copy;
    }

    return dst;
}
