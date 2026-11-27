#include <errno.h>
#include <kernel/com/dev/power.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/io/term.h>
#include <kernel/com/util.h>
#include <kernel/platform/power.h>
#include <salernos/devpower.h>

static int devpower_ioctl(void *devdata, u64 op, void *buf) {
    (void)devdata;
    (void)buf;
    if (op != DEVPOWER_IOCTL_SHUTDOWN) return ENOSYS;
    term_flush(NULL);
    power_shutdown();
}

static DevOps g_devpower_devops = {.type = VNODE_TYPE_CHARDEV,
                                   .ioctl = devpower_ioctl};

int devpower_init(void) {
    log("initializing /dev/power");
    return devfs_register(NULL, "power", 5, &g_devpower_devops, NULL, NULL);
}
