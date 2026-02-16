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

#include <stddef.h>
#include <stdint.h>

typedef enum com_profile_func {
    E_COM_PROFILE_FUNC_KMEMSET = 0,
    E_COM_PROFILE_FUNC_KMEMCPY,
    E_COM_PROFILE_FUNC_KMEMMOVE,
    E_COM_PROFILE_FUNC_KMEMCMP,
    E_COM_PROFILE_FUNC_KMEMCHR,
    E_COM_PROFILE_FUNC_KMEMRCHR,
    E_COM_PROFILE_FUNC_KSPINLOCK_ACQUIRE,
    E_COM_PROFILE_FUNC_KMUTEX_ACQUIRE,
    E_COM_PROFILE_FUNC_PMM_ALLOC,
    E_COM_PROFILE_FUNC_PMM_FREE,
    E_COM_PROFILE_FUNC_SCHED_YIELD,

    E_COM_PROFILE_FUNC__MAX
} com_profile_func_t;

typedef struct com_profile_func_data {
    uintmax_t real_time;
    uintmax_t cpu_time;
    size_t    num_calls;
} com_profile_func_data_t;

typedef struct com_syswide_profile {
    com_profile_func_data_t functions[E_COM_PROFILE_FUNC__MAX];
    com_profile_func_data_t syscalls[CONFIG_SYSCALL_MAX];
} com_syswide_profile_t;

typedef struct com_profiler_data {
    union {
        com_profile_func_t function_id;
        size_t             syscall_number;
    };
    uintmax_t real_start;
    uintmax_t thread_slept_start;
} com_profiler_data_t;

void                   com_sys_profiler_init(void);
com_syswide_profile_t *com_sys_profiler_get_syswide(void);
const char *com_sys_profiler_resolve_name(com_profile_func_t function_id);

// This is quite unconventional but it helps reduce code clutter

#if CONFIG_USE_PROFILER

com_profiler_data_t
     com_sys_profiler_start_function(com_profile_func_t function_id);
void com_sys_profiler_end_function(com_profiler_data_t *data);

com_profiler_data_t com_sys_profiler_start_syscall(size_t syscall_number);
void                com_sys_profiler_end_syscall(com_profiler_data_t *data);

#else

static inline com_profiler_data_t
com_sys_profiler_start_function(com_profile_func_t function_id) {
    (void)function_id;
    return (com_profiler_data_t){0};
}

static inline void com_sys_profiler_end_function(com_profiler_data_t *data) {
    (void)data;
}

static inline com_profiler_data_t
com_sys_profiler_start_syscall(size_t syscall_number) {
    (void)syscall_number;
    return (com_profiler_data_t){0};
}

static inline void com_sys_profiler_end_syscall(com_profiler_data_t *data) {
    (void)data;
}

#endif
