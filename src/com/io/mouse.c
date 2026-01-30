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

#include <errno.h>
#include <fcntl.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/io/mouse.h>
#include <kernel/com/sys/sched.h>
#include <lib/mem.h>
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
    int          ret   = 0;
    // TODO: sanitize buflen and make sure it's a multiple of the packet size,
    // otherwise we leave garbage in the rb
    kspinlock_acquire(&mouse->rb.lock);

    if (0 == KRINGBUFFER_AVAIL_READ(&mouse->rb) &&
        mouse->consolidated.present) {
        size_t copy_size = KMIN(buflen, sizeof(com_mouse_packet_t));
        kmemcpy(buf, &mouse->consolidated.packet, copy_size);
        kmemset(&mouse->consolidated.packet, sizeof(com_mouse_packet_t), 0);
        mouse->consolidated.present = false;
        *bytes_read                 = copy_size;
        goto end;
    }

    ret = kringbuffer_read_nolock(buf,
                                  bytes_read,
                                  &mouse->rb,
                                  buflen,
                                  sizeof(com_mouse_packet_t),
                                  !(O_NONBLOCK & flags),
                                  mouse_poll_callback,
                                  mouse,
                                  NULL);

end:
    kspinlock_release(&mouse->rb.lock);
    return ret;
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

    kspinlock_acquire(&mouse->rb.lock);
    if (KRINGBUFFER_AVAIL_READ(&mouse->rb) > 0 || mouse->consolidated.present) {
        out |= POLLIN;
    }
    kspinlock_release(&mouse->rb.lock);

    *revents = out;
    return 0;
}

static int mouse_stat(struct stat *out, void *devdata) {
    out->st_blksize = 512;
    out->st_ino     = (ino_t)devdata;
    out->st_mode    = 0666;
    out->st_dev     = 10 << 8;
    out->st_rdev    = 10 << 8;
    out->st_mode |= S_IFCHR;
    out->st_nlink = 1;
    return 0;
}

static com_dev_ops_t MouseDevOps = {.read      = mouse_read,
                                    .poll_head = mouse_poll_head,
                                    .poll      = mouse_poll,
                                    .stat      = mouse_stat};

int com_io_mouse_send_packet(com_mouse_t *mouse, com_mouse_packet_t *pkt) {
    kspinlock_acquire(&mouse->rb.lock);
    int rb_err = kringbuffer_write_nolock(NULL,
                                          &mouse->rb,
                                          pkt,
                                          sizeof(com_mouse_packet_t),
                                          sizeof(com_mouse_packet_t),
                                          false,
                                          mouse_poll_callback,
                                          mouse,
                                          NULL);

    // If we received EAGAIN, this means that the packet could not be written to
    // the ringbuffer and thus we consolidate the values as explained in mouse.h
    if (EAGAIN == rb_err) {
        mouse->consolidated.packet.buttons |= pkt->buttons;
        mouse->consolidated.packet.dx += pkt->dx;
        mouse->consolidated.packet.dy += pkt->dy;
        mouse->consolidated.packet.dz += pkt->dz;
        mouse->consolidated.present = true;
        mouse_poll_callback(mouse);
        rb_err = 0;
    }

    kspinlock_release(&mouse->rb.lock);
    return rb_err;
}

com_mouse_t *com_io_mouse_new(void) {
    size_t mouse_num = __atomic_fetch_add(&NextMouseNum, 1, __ATOMIC_SEQ_CST);
    KLOG("initializing mouse in /dev/mouse%zu", mouse_num);

    // Poor man's sprintf
    char mouse_num_str[24];
    kuitoa(mouse_num, mouse_num_str);
    char mouse_name[5 + 24] = {0}; // "mouse" + number
    kstrcpy(mouse_name, "mouse");
    kstrcpy(&mouse_name[5], mouse_num_str);

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
