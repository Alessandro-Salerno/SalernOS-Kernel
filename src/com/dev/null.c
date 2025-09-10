/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
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

#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/mm/slab.h>
#include <lib/util.h>
#include <poll.h>

struct devnull {
    com_poll_head_t pollhead;
};

static int dev_null_read(void     *buf,
                         size_t    buflen,
                         size_t   *bytes_read,
                         void     *devdata,
                         uintmax_t off,
                         uintmax_t flags) {
    (void)buf;
    (void)buflen;
    (void)devdata;
    (void)off;
    (void)flags;
    *bytes_read = 0;
    return 0;
}

static int dev_null_write(size_t   *bytes_written,
                          void     *devdata,
                          void     *buf,
                          size_t    buflen,
                          uintmax_t off,
                          uintmax_t flags) {
    (void)devdata;
    (void)buf;
    (void)off;
    (void)flags;
    *bytes_written = buflen;
    return 0;
}

static int dev_null_poll_head(com_poll_head_t **out, void *devdata) {
    struct devnull *devnull = devdata;
    *out                    = &devnull->pollhead;
    return 0;
}

static int dev_null_poll(short *revents, void *devdata, short events) {
    (void)devdata;
    (void)events;
    *revents = POLLIN | POLLOUT | POLLPRI;
    return 0;
}

static int dev_null_stat(struct stat *out, void *devdata) {
    out->st_blksize = 0;
    out->st_ino     = (ino_t)devdata;
    out->st_mode    = 0777;
    out->st_mode |= S_IFCHR;
    return 0;
}

static int dev_null_ioctl(void *devdata, uintmax_t op, void *buf) {
    (void)devdata;
    (void)op;
    (void)buf;
    return 0;
}

static com_dev_ops_t DevNullOps = {.read      = dev_null_read,
                                   .write     = dev_null_write,
                                   .poll_head = dev_null_poll_head,
                                   .poll      = dev_null_poll,
                                   .stat      = dev_null_stat,
                                   .ioctl     = dev_null_ioctl};

int com_dev_null_init(void) {
    KLOG("initializing /dev/null");

    struct devnull *devnull = com_mm_slab_alloc(sizeof(struct devnull));
    LIST_INIT(&devnull->pollhead.polled_list);

    int ret = com_fs_devfs_register(NULL,
                                    NULL,
                                    "null",
                                    4,
                                    &DevNullOps,
                                    devnull);

    if (0 != ret) {
        com_mm_slab_free(devnull, sizeof(struct devnull));
    }

    return ret;
}
