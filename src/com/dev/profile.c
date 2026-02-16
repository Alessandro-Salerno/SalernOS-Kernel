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

#include <arch/cpu.h>
#include <errno.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/sys/profiler.h>
#include <kernel/com/sys/syscall.h>
#include <lib/str.h>
#include <lib/util.h>
#include <salernos/devprofile.h>

static int devprofile_ioctl(void *devdata, uintmax_t op, void *buf) {
    (void)devdata;

    if (DEVPROFILE_IOCTL_GET_NUM_FUNCTIONS == op) {
        *(size_t *)buf = E_COM_PROFILE_FUNC__MAX;
        return 0;
    } else if (DEVPROFILE_IOCTL_GET_NUM_SYSCALLS == op) {
        com_sys_syscall_get_tables(NULL, NULL, (size_t *)buf);
        return 0;
    } else if (DEVPROFILE_IOCTL_GET_FUNCTIONS == op) {
        struct devprofile_fn_res *r        = buf;
        com_syswide_profile_t    *profiler = com_sys_profiler_get_syswide();

        for (com_profile_func_t i = 0; i < E_COM_PROFILE_FUNC__MAX; i++) {
            struct devprofile_fn_data *d    = &r->data[i];
            com_profile_func_data_t   *data = &profiler->functions[i];
            kstrcpy(d->name, com_sys_profiler_resolve_name(i));
            d->real_time_ns = ARCH_CPU_TIMESTAMP_TO_NS(data->real_time);
            d->cpu_time_ns  = ARCH_CPU_TIMESTAMP_TO_NS(data->cpu_time);
            d->num_calls    = data->num_calls;
        }

        return 0;
    } else if (DEVPROFILE_IOCTL_GET_SYSCALL == op) {
        struct devprofile_fn_res *r            = buf;
        size_t                    num_syscalls = 0;
        com_syscall_aux_t        *aux_syscalls;
        com_sys_syscall_get_tables(NULL, &aux_syscalls, &num_syscalls);
        com_syswide_profile_t *profiler = com_sys_profiler_get_syswide();

        for (size_t i = 0; i < num_syscalls; i++) {
            struct devprofile_fn_data *d    = &r->data[i];
            com_syscall_aux_t         *aux  = &aux_syscalls[i];
            com_profile_func_data_t   *data = &profiler->syscalls[i];
            kstrcpy(d->name, aux->name);
            d->real_time_ns = ARCH_CPU_TIMESTAMP_TO_NS(data->real_time);
            d->cpu_time_ns  = ARCH_CPU_TIMESTAMP_TO_NS(data->cpu_time);
            d->num_calls    = data->num_calls;
        }

        return 0;
    }

    return ENOSYS;
}

static com_dev_ops_t DevprofileDevops = {.ioctl = devprofile_ioctl};

int com_dev_profile_init(void) {
#if CONFIG_USE_PROFILER
    KLOG("initializing /dev/fb0 fbdev")
    return com_fs_devfs_register(NULL,
                                 NULL,
                                 "profile",
                                 7,
                                 &DevprofileDevops,
                                 NULL);
#else
    return 0;
#endif
}
