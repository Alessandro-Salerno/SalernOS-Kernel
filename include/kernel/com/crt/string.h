#pragma once

#include <kernel/com/util.h>

void *memset(void *dst, int c, usize n);
int memcmp(const void *s1, const void *s2, usize n);
void *memcpy(void *restrict dst, const void *restrict src, usize n);
void *memchr(const void *s, int c, usize n);
void *memrchr(const void *s, int c, usize n);
void *memmove(void *dst, const void *src, usize n);
void *memrepcpy(void *dst, const void *src, usize n, usize rep);

int strcmp(const char *s1, const char *s2);
usize strlen(const char *s);
char *strcpy(char *dst, const char *src);

const char *uitoa(u64 val, char *s);
const char *itoa(i64 val, char *s);
const char *xuitoa(u64 val, char *s, bool have_leading);
void strpathpenult(const char *s, usize len, usize *out_penult_len,
                   usize *out_end_idx, usize *out_end_len);
