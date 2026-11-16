#ifndef _SALERNOS_DEVFAILNTH_H
#define _SALERNOS_DEVFAILNTH_H

#include <asm/ioctl.h>
#include <stddef.h>

// After SET with n > 0, the n-th allocation made by the calling thread fails
// and injection stops. GET returns the allocations left before the failure, or
// 0 once it happened (or if nothing was set)
#define DEVFAILNTH_IOCTL_SET _IOW('N', 0x00, size_t)
#define DEVFAILNTH_IOCTL_GET _IOR('N', 0x01, size_t)

// Number of live slab objects, to look for leaks in error paths
#define DEVFAILNTH_IOCTL_GET_SLAB_OBJECTS _IOR('N', 0x02, size_t)

#endif
