#pragma once

#include <kernel/com/util.h>

#define LIBHELP_FAST_MEMSET(dst, c, n)      \
    do {                                    \
        void *__d = (dst);                  \
        usize __n = (n);                    \
        asm volatile("cld; rep stosb"       \
                     : "+D"(__d), "+c"(__n) \
                     : "a"(c)               \
                     : "memory");           \
    } while (0)

#define LIBHELP_FAST_MEMCPY(dst, src, n)               \
    do {                                               \
        void *__d = (dst);                             \
        const void *__s = (src);                       \
        usize __n = (n);                               \
        asm volatile("cld; rep movsb"                  \
                     : "+D"(__d), "+S"(__s), "+c"(__n) \
                     :                                 \
                     : "flags", "memory");             \
    } while (0)

#define LIBHELP_FAST_REVERSE_MEMCPY(dst, src, n)                  \
    do {                                                          \
        usize __n = (n);                                          \
        void *__d = (unsigned char *)(dst) + __n - 1;             \
        const void *__s = (const unsigned char *)(src) + __n - 1; \
        asm volatile("std; rep movsb; cld"                        \
                     : "+D"(__d), "+S"(__s), "+c"(__n)            \
                     :                                            \
                     : "flags", "memory");                        \
    } while (0)
