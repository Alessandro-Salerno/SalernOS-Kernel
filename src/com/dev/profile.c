#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/syscall.h>
#include <kernel/com/util.h>
#include <salernos/devprofile.h>

static int devprofile_copy_row(struct devprofile_fn_res *r, usize i,
                               const char *name, ProfileFuncData *data) {
    struct devprofile_fn_data d = {0};
    if (name != NULL) {
        memcpy(d.name, name, min(strlen(name), sizeof(d.name) - 1));
    }
    d.real_time_ns = cpu_timestamp_to_ns(data->real_time);
    d.cpu_time_ns = cpu_timestamp_to_ns(data->cpu_time);
    d.num_calls = data->num_calls;
    return memcpy_to_user(&r->data[i], &d, sizeof(d));
}

static int devprofile_ioctl(void *devdata, u64 op, void *buf) {
    (void)devdata;

    if (op == DEVPROFILE_IOCTL_GET_NUM_FUNCTIONS) {
        usize num = PROFILE_FUNC__MAX;
        return memcpy_to_user(buf, &num, sizeof(num));
    } else if (op == DEVPROFILE_IOCTL_GET_NUM_SYSCALLS) {
        usize num = 0;
        syscall_get_tables(NULL, NULL, &num);
        return memcpy_to_user(buf, &num, sizeof(num));
    } else if (op == DEVPROFILE_IOCTL_GET_FUNCTIONS) {
        SysWideProfile *profiler = profiler_get_syswide();

        for (ProfileFunc i = 0; i < PROFILE_FUNC__MAX; i++) {
            int ret = devprofile_copy_row(buf, i, profiler_resolve_name(i),
                                          &profiler->functions[i]);
            if (ret != 0) return ret;
        }

        return 0;
    } else if (op == DEVPROFILE_IOCTL_GET_SYSCALLS) {
        usize num_syscalls = 0;
        SyscallAux *aux_syscalls;
        syscall_get_tables(NULL, &aux_syscalls, &num_syscalls);
        SysWideProfile *profiler = profiler_get_syswide();

        for (usize i = 0; i < num_syscalls; i++) {
            int ret = devprofile_copy_row(buf, i, aux_syscalls[i].name,
                                          &profiler->syscalls[i]);
            if (ret != 0) return ret;
        }

        return 0;
    }

    return ENOSYS;
}

static DevOps g_devprofile_devops = {.type = VNODE_TYPE_CHARDEV,
                                     .ioctl = devprofile_ioctl};

int profile_init(void) {
#if CONFIG_USE_PROFILER
    log("initializing /dev/fb0 fbdev");
    return devfs_register(NULL, "profile", 7, &g_devprofile_devops, NULL, NULL);
#else
    return 0;
#endif
}
