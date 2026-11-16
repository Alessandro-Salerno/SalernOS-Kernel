#include <errno.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/dev/failnth.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/io/log.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/failnth.h>
#include <kernel/com/util.h>
#include <salernos/devfailnth.h>

#if CONFIG_FAIL_NTH
static int devfailnth_ioctl(void *devdata, u64 op, void *buf) {
    (void)devdata;

    if (op == DEVFAILNTH_IOCTL_SET) {
        usize n;
        int ret = memcpy_from_user(&n, buf, sizeof(n));
        if (ret != 0) return ret;
        failnth_set(n);
        return 0;
    } else if (op == DEVFAILNTH_IOCTL_GET) {
        usize n = failnth_get();
        return memcpy_to_user(buf, &n, sizeof(n));
    } else if (op == DEVFAILNTH_IOCTL_GET_SLAB_OBJECTS) {
        usize n = slab_get_num_objects();
        return memcpy_to_user(buf, &n, sizeof(n));
    }

    return ENOSYS;
}

static DevOps g_devfailnth_devops = {.type = VNODE_TYPE_CHARDEV,
                                     .ioctl = devfailnth_ioctl};
#endif

int failnth_init(void) {
#if CONFIG_FAIL_NTH
    log("initializing /dev/failnth");
    return devfs_register(NULL, "failnth", 7, &g_devfailnth_devops, NULL, NULL);
#else
    return 0;
#endif
}
