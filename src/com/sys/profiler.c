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
| but WITHOUT ANY WARRANTY{} without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#include <arch/cpu.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/thread.h>

static com_syswide_profile_t SystemProfile       = {0};
static bool                  ProfilerInitialized = false;

static inline void profiler_calc_elapsed_time(uintmax_t *out_real_time,
                                              uintmax_t *out_cpu_time,
                                              com_profiler_data_t *data) {
    com_thread_t *curr_thread  = ARCH_CPU_GET_THREAD();
    uintmax_t     end_real     = ARCH_CPU_GET_TIMESTAMP();
    uintmax_t     real_elapsed = end_real - data->real_start;

    *out_real_time = real_elapsed;

    if (NULL != curr_thread) {
        uintmax_t end_thread_slept = curr_thread->time_slept;
        uintmax_t time_slept = end_thread_slept - data->thread_slept_start;
        *out_cpu_time        = real_elapsed - time_slept;
    } else {
        *out_cpu_time = 0;
    }
}

static inline com_profiler_data_t profiler_init_data(void) {
    com_profiler_data_t data = {.real_start = ARCH_CPU_GET_TIMESTAMP(), 0};
    com_thread_t       *curr_thread = ARCH_CPU_GET_THREAD();
    if (NULL != curr_thread) {
        data.thread_slept_start = curr_thread->time_slept;
    }
    return data;
}

void com_sys_profiler_init(void) {
    ProfilerInitialized = true;
}

com_syswide_profile_t *com_sys_profiler_get_syswide(void) {
    return &SystemProfile;
}

// TODO: this is very clunky, we can probably do something with debug syms
const char *com_sys_profiler_resolve_name(com_profile_func_t function_id) {
    switch (function_id) {
        case E_COM_PROFILE_FUNC_KMEMSET:
            return "kmemset";
        case E_COM_PROFILE_FUNC_KMEMCPY:
            return "kmemcpy";
        case E_COM_PROFILE_FUNC_KMEMMOVE:
            return "kmemmove";
        case E_COM_PROFILE_FUNC_KMEMCMP:
            return "kmemcmp";
        case E_COM_PROFILE_FUNC_KMEMCHR:
            return "kmemchr";
        case E_COM_PROFILE_FUNC_KMEMRCHR:
            return "kmemrchr";
        case E_COM_PROFILE_FUNC_KSPINLOCK_ACQUIRE:
            return "kspinlock_acquire";
        case E_COM_PROFILE_FUNC_KMUTEX_ACQUIRE:
            return "kmutex_acquire";
        case E_COM_PROFILE_FUNC_PMM_ALLOC:
            return "com_mm_pmm_alloc";
        case E_COM_PROFILE_FUNC_PMM_FREE:
            return "com_mm_pmm_free";
        case E_COM_PROFILE_FUNC_SCHED_YIELD:
            return "com_sys_sched_yield_nolock";
        case E_COM_PROFILE_FUNC_ELF_LOAD:
            return "com_sys_elf64_load";
        default:
            break;
    }

    return "(unknown)";
}

#if CONFIG_USE_PROFILER

com_profiler_data_t
com_sys_profiler_start_function(com_profile_func_t function_id) {
    if (!ProfilerInitialized) {
        return (com_profiler_data_t){0};
    }
    com_profiler_data_t data = profiler_init_data();
    data.function_id         = function_id;
    return data;
}

void com_sys_profiler_end_function(com_profiler_data_t *data) {
    if (!ProfilerInitialized) {
        return;
    }

    uintmax_t real_elapsed;
    uintmax_t cpu_elapsed;
    profiler_calc_elapsed_time(&real_elapsed, &cpu_elapsed, data);

    com_profile_func_data_t
        *function_data = &SystemProfile.functions[data->syscall_number];
    __atomic_add_fetch(&function_data->num_calls, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&function_data->real_time,
                       real_elapsed,
                       __ATOMIC_RELAXED);
    __atomic_add_fetch(&function_data->cpu_time, cpu_elapsed, __ATOMIC_RELAXED);
}

com_profiler_data_t com_sys_profiler_start_syscall(size_t syscall_number) {
    if (!ProfilerInitialized) {
        return (com_profiler_data_t){0};
    }
    com_profiler_data_t data = profiler_init_data();
    data.syscall_number      = syscall_number;
    return data;
}

void com_sys_profiler_end_syscall(com_profiler_data_t *data) {
    if (!ProfilerInitialized) {
        return;
    }

    uintmax_t real_elapsed;
    uintmax_t cpu_elapsed;
    profiler_calc_elapsed_time(&real_elapsed, &cpu_elapsed, data);

    com_profile_func_data_t
        *syscall_data = &SystemProfile.syscalls[data->syscall_number];
    __atomic_add_fetch(&syscall_data->num_calls, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&syscall_data->real_time,
                       real_elapsed,
                       __ATOMIC_RELAXED);
    __atomic_add_fetch(&syscall_data->cpu_time, cpu_elapsed, __ATOMIC_RELAXED);
}

#endif
