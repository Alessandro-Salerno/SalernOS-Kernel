#ifndef _SALERNOS_KTTY_H
#define _SALERNOS_KTTY_H

#include <asm/ioctl.h>

#define KTTY_IOCTL_SETMODE 0x4B3A
#define KTTY_MODE_TEXT     0x00
#define KTTY_MODE_GRAPHICS 0x01

#define KTTY_IOCTL_SETSWITCHMODE _IOW('k', 0x00, struct ktty_switchmode)
#define KTTY_SWITCH_AUTO         0x00
#define KTTY_SWITCH_PROCESS      0x01

#define KTTY_IOCTL_RELEASE _IO('k', 0x01)
#define KTTY_RELEASE_DENY  0x00
#define KTTY_RELEASE_ALLOW 0x01

#define KTTY_IOCTL_SWITCH _IO('k', 0x02)
#define KTTY_IOCTL_WAITFG _IO('k', 0x03)
#define KTTY_IOCTL_GETNUM _IOR('k', 0x04, int)

struct ktty_switchmode {
    int mode;
    int relsig;
    int acqsig;
};

#endif
