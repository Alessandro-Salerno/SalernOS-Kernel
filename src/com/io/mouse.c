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

#include <fcntl.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/io/mouse.h>
#include <kernel/com/sys/sched.h>
#include <lib/str.h>
#include <poll.h>

static size_t NextMouseNum = 0;

static void mouse_poll_callback(void *arg) {
    com_mouse_t *mouse = arg;

    kspinlock_acquire(&mouse->pollhead.lock);
    com_polled_t *polled, *_;
    LIST_FOREACH_SAFE(polled, &mouse->pollhead.polled_list, polled_list, _) {
        kspinlock_acquire(&polled->poller->lock);
        com_sys_sched_notify_all(&polled->poller->waiters);
        kspinlock_release(&polled->poller->lock);
    }
    kspinlock_release(&mouse->pollhead.lock);
}

static int mouse_read(void     *buf,
                      size_t    buflen,
                      size_t   *bytes_read,
                      void     *devdata,
                      uintmax_t off,
                      uintmax_t flags) {
    (void)off;
    com_mouse_t *mouse = devdata;
    return kringbuffer_read(buf,
                            bytes_read,
                            &mouse->rb,
                            buflen,
                            sizeof(com_mouse_packet_t),
                            !(O_NONBLOCK & flags),
                            mouse_poll_callback,
                            mouse,
                            NULL);
}

static int mouse_poll_head(com_poll_head_t **out, void *devdata) {
    com_mouse_t *mouse = devdata;
    *out               = &mouse->pollhead;
    return 0;
}

static int mouse_poll(short *revents, void *devdata, short events) {
    (void)events;
    com_mouse_t *mouse = devdata;
    short        out   = 0;

    if (KRINGBUFFER_AVAIL_READ(&mouse->rb) > 0) {
        out |= POLLIN;
    }
    if (KRINGBUFFER_AVAIL_WRITE(&mouse->rb) > sizeof(com_mouse_packet_t)) {
        out |= POLLOUT;
    }

    *revents = out;
    return 0;
}

static int mouse_stat(struct stat *out, void *devdata) {
    out->st_blksize = sizeof(com_mouse_packet_t);
    out->st_ino     = (ino_t)devdata;
    out->st_mode    = 0777;
    out->st_mode |= S_IFCHR;
    return 0;
}

static com_dev_ops_t MouseDevOps = {.read      = mouse_read,
                                    .poll_head = mouse_poll_head,
                                    .poll      = mouse_poll,
                                    .stat      = mouse_stat};

int com_io_mouse_send_packet(com_mouse_t *mouse, com_mouse_packet_t *pkt) {
    return kringbuffer_write(NULL,
                             &mouse->rb,
                             pkt,
                             sizeof(com_mouse_packet_t),
                             sizeof(com_mouse_packet_t),
                             true,
                             mouse_poll_callback,
                             mouse,
                             NULL);
}

com_mouse_t *com_io_mouse_new(void) {
    size_t mouse_num = __atomic_fetch_add(&NextMouseNum, 1, __ATOMIC_SEQ_CST);
    KLOG("initializing mouse in /dev/mouse%u", mouse_num);

    // Poor man's sprintf
    char mouse_num_str[24];
    kuitoa(mouse_num, mouse_num_str);
    char mouse_name[8 + 24] = {0}; // "mouse" + number
    kstrcpy(mouse_name, "mouse");
    kstrcpy(&mouse_name[8], mouse_num_str);

    com_mouse_t *mouse = com_mm_slab_alloc(sizeof(com_mouse_t));
    KRINGBUFFER_INIT(&mouse->rb);
    LIST_INIT(&mouse->pollhead.polled_list);

    int devfs_ret = com_fs_devfs_register(&mouse->vnode,
                                          NULL,
                                          mouse_name,
                                          kstrlen(mouse_name),
                                          &MouseDevOps,
                                          mouse);
    if (0 != devfs_ret) {
        com_mm_slab_free(mouse, sizeof(com_mouse_t));
        return NULL;
    }

    return mouse;
}
