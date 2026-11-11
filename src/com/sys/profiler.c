#include <arch/cpu.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/thread.h>
#include <kernel/com/util.h>

static SysWideProfile g_system_profile = {0};
static bool g_profiler_initialized = false;

static inline void profiler_calc_elapsed_time(ProfilerData *data,
                                              u64 *out_real_time,
                                              u64 *out_cpu_time) {
    Thread *curr_thread = cpu_get_thread();
    // Opposite order to the start, so a preemption in between can only
    // inflate the Cpu time, never make it negative
    u64 end_thread_slept = curr_thread != NULL ? curr_thread->time_slept : 0;
    u64 end_real = cpu_get_timestamp();
    u64 real_elapsed = end_real - data->real_start;

    *out_real_time = real_elapsed;

    if (curr_thread != NULL) {
        u64 time_slept = end_thread_slept - data->thread_slept_start;
        *out_cpu_time = real_elapsed - time_slept;
    } else {
        *out_cpu_time = 0;
    }
}

static inline ProfilerData profiler_init_data(void) {
    ProfilerData data = {.real_start = cpu_get_timestamp(), 0};
    Thread *curr_thread = cpu_get_thread();
    if (curr_thread != NULL) data.thread_slept_start = curr_thread->time_slept;
    return data;
}

void profiler_init(void) { g_profiler_initialized = true; }

SysWideProfile *profiler_get_syswide(void) { return &g_system_profile; }

// TODO: this is very clunky, we can probably do something with debug syms
const char *profiler_resolve_name(ProfileFunc function_id) {
    switch (function_id) {
        case PROFILE_FUNC_MEMSET:
            return "memset";
        case PROFILE_FUNC_MEMCPY:
            return "memcpy";
        case PROFILE_FUNC_MEMMOVE:
            return "memmove";
        case PROFILE_FUNC_MEMCMP:
            return "memcmp";
        case PROFILE_FUNC_MEMCHR:
            return "memchr";
        case PROFILE_FUNC_MEMRCHR:
            return "memrchr";
        case PROFILE_FUNC_SPINLOCK_ACQ:
            return "spinlock_acq";
        case PROFILE_FUNC_MUTEX_ACQ:
            return "mutex_acq";
        case PROFILE_FUNC_PMM_ALLOC:
            return "pmm_alloc";
        case PROFILE_FUNC_PMM_FREE:
            return "pmm_free";
        case PROFILE_FUNC_SCHED_YIELD:
            return "sched_yield_nolock";
        case PROFILE_FUNC_ELF_LOAD:
            return "elf64_load";
        case PROFILE_FUNC_VMM_HANDLE_FAULT:
            return "vmm_handle_fault";
        default:
            break;
    }

    return "(unknown)";
}

#if CONFIG_USE_PROFILER

ProfilerData profiler_start_function(ProfileFunc function_id) {
    if (!g_profiler_initialized) return (ProfilerData){0};
    ProfilerData data = profiler_init_data();
    data.function_id = function_id;
    return data;
}

void profiler_end_function(ProfilerData *data) {
    if (!g_profiler_initialized) return;

    u64 real_elapsed;
    u64 cpu_elapsed;
    profiler_calc_elapsed_time(data, &real_elapsed, &cpu_elapsed);

    ProfileFuncData *function_data = &g_system_profile
                                          .functions[data->syscall_number];
    __atomic_add_fetch(&function_data->num_calls, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&function_data->real_time, real_elapsed,
                       __ATOMIC_RELAXED);
    __atomic_add_fetch(&function_data->cpu_time, cpu_elapsed, __ATOMIC_RELAXED);
}

ProfilerData profiler_start_syscall(usize syscall_number) {
    if (!g_profiler_initialized) return (ProfilerData){0};
    ProfilerData data = profiler_init_data();
    data.syscall_number = syscall_number;
    return data;
}

void profiler_end_syscall(ProfilerData *data) {
    if (!g_profiler_initialized) return;

    u64 real_elapsed;
    u64 cpu_elapsed;
    profiler_calc_elapsed_time(data, &real_elapsed, &cpu_elapsed);

    ProfileFuncData *syscall_data = &g_system_profile
                                         .syscalls[data->syscall_number];
    __atomic_add_fetch(&syscall_data->num_calls, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&syscall_data->real_time, real_elapsed,
                       __ATOMIC_RELAXED);
    __atomic_add_fetch(&syscall_data->cpu_time, cpu_elapsed, __ATOMIC_RELAXED);
}

#endif
