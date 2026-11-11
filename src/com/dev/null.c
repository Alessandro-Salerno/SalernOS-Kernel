#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/util.h>
#include <poll.h>

struct DevNull {
    PollHead pollhead;
};

static int dev_null_read(void *buf, usize buflen, void *devdata, u64 off,
                         u64 flags, usize *out_bytes_read) {
    (void)buf;
    (void)buflen;
    (void)devdata;
    (void)off;
    (void)flags;
    *out_bytes_read = 0;
    return 0;
}

static int dev_null_write(void *devdata, void *buf, usize buflen, u64 off,
                          u64 flags, usize *out_bytes_written) {
    (void)devdata;
    (void)buf;
    (void)off;
    (void)flags;
    *out_bytes_written = buflen;
    return 0;
}

static int dev_null_poll_head(void *devdata, PollHead **out_poll_head) {
    struct DevNull *devnull = devdata;
    *out_poll_head = &devnull->pollhead;
    return 0;
}

static int dev_null_poll(void *devdata, short events, short *out_revents) {
    (void)devdata;
    (void)events;
    *out_revents = POLLIN | POLLOUT | POLLPRI;
    return 0;
}

static int dev_null_stat(void *devdata, struct stat *out_stat) {
    (void)devdata;
    out_stat->st_blksize = 0;
    out_stat->st_mode = 0777;
    return 0;
}

static int dev_null_ioctl(void *devdata, u64 op, void *buf) {
    (void)devdata;
    (void)op;
    (void)buf;
    return 0;
}

static DevOps g_dev_null_ops = {.type = VNODE_TYPE_CHARDEV,
                                .read = dev_null_read,
                                .write = dev_null_write,
                                .poll_head = dev_null_poll_head,
                                .poll = dev_null_poll,
                                .stat = dev_null_stat,
                                .ioctl = dev_null_ioctl};

int null_init(void) {
    log("initializing /dev/null");

    struct DevNull *devnull = slab_alloc(sizeof(struct DevNull));
    LIST_INIT(&devnull->pollhead.polled_list);

    int ret = devfs_register(NULL, "null", 4, &g_dev_null_ops, devnull, NULL);

    if (ret != 0) slab_free(devnull, sizeof(struct DevNull));

    return ret;
}
