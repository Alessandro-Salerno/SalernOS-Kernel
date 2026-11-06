#ifndef _SALERNOS_SYSCALL_STR_H
#define _SALERNOS_SYSCALL_STR_H

#include <stddef.h>

struct syscall_str {
    const char *str;
    size_t      len;
};

#endif
