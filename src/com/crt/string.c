#include <arch/libhelp.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/util.h>

void *memset(void *dst, int c, usize n) {
    ProfilerData profiler_data = profiler_start_function(PROFILE_FUNC_MEMSET);
#ifdef LIBHELP_FAST_MEMSET
    LIBHELP_FAST_MEMSET(dst, c, n);
#else
    for (usize i = 0; i < n; i++) ((u8 *)dst)[i] = (u8)c;
#endif
    profiler_end_function(&profiler_data);
    return dst;
}

int memcmp(const void *s1, const void *s2, usize n) {
    ProfilerData profiler_data = profiler_start_function(PROFILE_FUNC_MEMCMP);
    const u8 *b1 = s1;
    const u8 *b2 = s2;
    int ret = 0;
    for (usize i = 0; i < n && ret == 0; i++) ret = b1[i] - b2[i];

    profiler_end_function(&profiler_data);
    return ret;
}

void *memcpy(void *restrict dst, const void *restrict src, usize buffsize) {
    ProfilerData profiler_data = profiler_start_function(PROFILE_FUNC_MEMCPY);
#ifdef LIBHELP_FAST_MEMCPY
    LIBHELP_FAST_MEMCPY(dst, src, buffsize);
#else
    u64 src_off = (u64)src;
    u64 dst_off = (u64)dst;

    for (u64 i = 0; i < buffsize; i++) {
        u8 val = *(u8 *)(src_off + i);
        *(u8 *)(dst_off + i) = val;
    }
#endif
    profiler_end_function(&profiler_data);
    return dst;
}

void *memchr(const void *str, int c, usize n) {
    ProfilerData profiler_data = profiler_start_function(PROFILE_FUNC_MEMCHR);
    const unsigned char *s = str;
    for (usize i = 0; i < n; i++) {
        if ((unsigned char)c == s[i]) {
            profiler_end_function(&profiler_data);
            return (void *)&s[i];
        }
    }

    profiler_end_function(&profiler_data);
    return NULL;
}

void *memrchr(const void *str, int c, usize n) {
    ProfilerData profiler_data = profiler_start_function(PROFILE_FUNC_MEMRCHR);
    const unsigned char *s = str;
    c = (unsigned char)c;

    while (n--) {
        if (s[n] == c) {
            profiler_end_function(&profiler_data);
            return (void *)(s + n);
        }
    }

    profiler_end_function(&profiler_data);
    return NULL;
}

void *memmove(void *dst, const void *src, usize n) {
    ProfilerData profiler_data = profiler_start_function(PROFILE_FUNC_MEMMOVE);
    if (src > dst) {
        memcpy(dst, src, n);
    } else if (src < dst) {
#ifdef LIBHELP_FAST_REVERSE_MEMCPY
        LIBHELP_FAST_REVERSE_MEMCPY(dst, src, n);
#else
        u8 *pdest = (u8 *)dst;
        const u8 *psrc = (const u8 *)src;
        for (usize i = n; i > 0; i--) pdest[i - 1] = psrc[i - 1];
#endif
    }

    profiler_end_function(&profiler_data);
    return dst;
}

void *memrepcpy(void *dst, const void *src, usize buffsize, usize rep) {
    if (rep == 0 || buffsize == 0) return NULL;

    memcpy(dst, src, buffsize);
    usize copied = buffsize;
    usize dst_size = buffsize * rep;

    while (copied < dst_size) {
        usize to_copy = copied;
        if (copied + to_copy > dst_size) to_copy = dst_size - copied;
        memcpy((u8 *)dst + copied, dst, to_copy);
        copied += to_copy;
    }

    return dst;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 != 0 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

usize strlen(const char *s) {
    usize n = 0;
    for (; *s != 0; s++) n++;
    return n;
}

char *strcpy(char *dst, const char *src) {
    usize n = 0;
    for (; src[n] != 0; n++) dst[n] = src[n];
    dst[n] = 0;
    return dst;
}

const char *uitoa(u64 val, char *s) {
    u64 size_test = val;
    u8 size = 0;
    u8 idx = 0;

    while (size_test >= 10) {
        size_test /= 10;
        size++;
    }

    while (idx < size) {
        u8 rem = val % 10;
        val /= 10;
        s[size - idx] = rem + '0';
        idx++;
    }

    u8 rem = val % 10;
    val /= 10;
    s[size - idx] = rem + '0';
    s[size + 1] = 0;

    return s;
}

const char *itoa(i64 val, char *s) {
    u64 size_test = 10;
    u8 size = 0;
    u8 idx = 0;
    u8 is_negative = 0;

    is_negative = val < 0;
    s[0] = (val < 0) ? '-' : '+';

    if (val < 0) val *= -1;

    while (val / size_test > 0) {
        size_test *= 10;
        size++;
    }

    while (idx < size) {
        u8 rem = val % 10;
        val /= 10;
        s[is_negative + size - idx] = rem + '0';
        idx++;
    }

    u8 rem = val % 10;
    val /= 10;
    s[is_negative + size - idx] = rem + '0';
    s[is_negative + size + 1] = 0;

    return s;
}

const char *xuitoa(u64 val, char *s, bool have_leading) {
    u64 size_test = val;
    u8 size = 0;
    u8 idx = 0;

    while (size_test >= 16) {
        size_test /= 16;
        size++;
    }

    usize print_size = size;

    if (have_leading) {
        u64 diff = 16 - size;
        print_size = 15;
        for (u64 i = 0; i < diff; i++) s[i] = '0';
    }

    while (idx < size) {
        u8 rem = val % 16;
        val /= 16;
        if (rem < 10) s[print_size - idx] = rem + '0';
        else s[print_size - idx] = rem - 10 + 'A';
        idx++;
    }

    u8 rem = val % 16;
    val /= 16;
    if (rem < 10) s[print_size - idx] = rem + '0';
    else s[print_size - idx] = rem - 10 + 'A';
    s[print_size + 1] = 0;

    return s;
}

// TODO: adopt this everywhere I calculate the seocnd-last index or similar
void strpathpenult(const char *s, usize len, usize *out_penult_len,
                   usize *out_end_idx, usize *out_end_len) {
    while (len > 0 && s[len - 1] == '/') len--;

    char *end = memrchr(s, '/', len);

    usize plen = 0;
    usize eidx = 0;
    usize elen = len;

    if (end != NULL) {
        plen = end - s;
        eidx = plen + 1;
        elen = len - eidx;
    }

    if (out_penult_len != NULL) *out_penult_len = plen;
    if (out_end_idx != NULL) *out_end_idx = eidx;
    if (out_end_len != NULL) *out_end_len = elen;
}
