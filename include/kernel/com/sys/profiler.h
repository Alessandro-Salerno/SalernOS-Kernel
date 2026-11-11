#pragma once

#include <kernel/com/util.h>

typedef enum ProfileFunc {
    PROFILE_FUNC_MEMSET = 0,
    PROFILE_FUNC_MEMCPY,
    PROFILE_FUNC_MEMMOVE,
    PROFILE_FUNC_MEMCMP,
    PROFILE_FUNC_MEMCHR,
    PROFILE_FUNC_MEMRCHR,
    PROFILE_FUNC_SPINLOCK_ACQ,
    PROFILE_FUNC_MUTEX_ACQ,
    PROFILE_FUNC_PMM_ALLOC,
    PROFILE_FUNC_PMM_FREE,
    PROFILE_FUNC_SCHED_YIELD,
    PROFILE_FUNC_ELF_LOAD,
    PROFILE_FUNC_VMM_HANDLE_FAULT,

    PROFILE_FUNC__MAX
} ProfileFunc;

typedef struct ProfileFuncData {
    u64 real_time;
    u64 cpu_time;
    usize num_calls;
} ProfileFuncData;

typedef struct SysWideProfile {
    ProfileFuncData functions[PROFILE_FUNC__MAX];
    ProfileFuncData syscalls[CONFIG_SYSCALL_MAX];
} SysWideProfile;

typedef struct ProfilerData {
    union {
        ProfileFunc function_id;
        usize syscall_number;
    };
    u64 real_start;
    u64 thread_slept_start;
} ProfilerData;

void profiler_init(void);
SysWideProfile *profiler_get_syswide(void);
const char *profiler_resolve_name(ProfileFunc function_id);

// This is quite unconventional but it helps reduce code clutter

#if CONFIG_USE_PROFILER

ProfilerData profiler_start_function(ProfileFunc function_id);
void profiler_end_function(ProfilerData *data);

ProfilerData profiler_start_syscall(usize syscall_number);
void profiler_end_syscall(ProfilerData *data);

#else

static inline ProfilerData profiler_start_function(ProfileFunc function_id) {
    (void)function_id;
    return (ProfilerData){0};
}

static inline void profiler_end_function(ProfilerData *data) { (void)data; }

static inline ProfilerData profiler_start_syscall(usize syscall_number) {
    (void)syscall_number;
    return (ProfilerData){0};
}

static inline void profiler_end_syscall(ProfilerData *data) { (void)data; }

#endif
