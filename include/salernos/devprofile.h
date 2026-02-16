#ifndef _SALERNOS_DEVPROFILE_H
#define _SALERNOS_DEVPROFILE_H

#include <asm/ioctl.h>
#include <stddef.h>
#include <stdint.h>

#define DEVPROFILE_IOCTL_GET_NUM_FUNCTIONS _IOR('P', 0X00, size_t)
#define DEVPROFILE_IOCTL_GET_NUM_SYSCALLS  _IOR('P', 0x01, size_t)

#define DEVPROFILE_IOCTL_GET_FUNCTIONS _IOR('P', 0X02, struct devprofile_fn_res)
#define DEVPROFILE_IOCTL_GET_SYSCALL   _IOR('P', 0X03, struct devprofile_fn_res)

#define DEVPROFILE_SIZEOF_FN_RES(n) ((n) * sizeof(struct devprofile_fn_data))

struct devprofile_fn_data {
    char      name[64];
    uintmax_t real_time_ns;
    uintmax_t cpu_time_ns;
    size_t    num_calls;
};

struct devprofile_fn_res {
    uint64_t                  _rsvd;
    struct devprofile_fn_data data[];
};

#endif
