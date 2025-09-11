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
#include <kernel/com/fs/poll.h>
#include <kernel/com/io/keyboard.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/sched.h>
#include <lib/str.h>
#include <poll.h>
#include <stddef.h>

#define __DEFAULT_LAYOUT(name) com_io_keyboard_layout_##name
#define _DEFAULT_LAYOUT(name)  __DEFAULT_LAYOUT(name)
#define DEFAULT_LAYOUT         _DEFAULT_LAYOUT(CONFIG_DEFAULT_KBD_LAYOUT)

static size_t NextKeyboardNum = 0;

static void keyboard_poll_callback(void *arg) {
    com_keyboard_t *keyboard = arg;

    kspinlock_acquire(&keyboard->pollhead.lock);
    com_polled_t *polled, *_;
    LIST_FOREACH_SAFE(polled, &keyboard->pollhead.polled_list, polled_list, _) {
        kspinlock_acquire(&polled->poller->lock);
        com_sys_sched_notify_all(&polled->poller->waiters);
        kspinlock_release(&polled->poller->lock);
    }
    kspinlock_release(&keyboard->pollhead.lock);
}

static int keyboard_read(void     *buf,
                         size_t    buflen,
                         size_t   *bytes_read,
                         void     *devdata,
                         uintmax_t off,
                         uintmax_t flags) {
    (void)off;
    com_keyboard_t *keyboard = devdata;
    return kringbuffer_read(buf,
                            bytes_read,
                            &keyboard->rb,
                            buflen,
                            sizeof(com_kbd_packet_t),
                            !(O_NONBLOCK & flags),
                            keyboard_poll_callback,
                            keyboard,
                            NULL);
}

static int keyboard_poll_head(com_poll_head_t **out, void *devdata) {
    com_keyboard_t *keyboard = devdata;
    *out                     = &keyboard->pollhead;
    return 0;
}

static int keyboard_poll(short *revents, void *devdata, short events) {
    (void)events;
    com_keyboard_t *keyboard = devdata;
    short           out      = 0;

    if (KRINGBUFFER_AVAIL_READ(&keyboard->rb) > 0) {
        out |= POLLIN;
    }
    if (KRINGBUFFER_AVAIL_WRITE(&keyboard->rb) > sizeof(com_kbd_packet_t)) {
        out |= POLLOUT;
    }

    *revents = out;
    return 0;
}

static int keyboard_stat(struct stat *out, void *devdata) {
    out->st_blksize = 512;
    out->st_ino     = (ino_t)devdata;
    out->st_mode    = 0666;
    out->st_mode |= S_IFCHR;
    out->st_dev   = (10 << 8);
    out->st_rdev  = (10 << 8);
    out->st_nlink = 1;
    return 0;
}

static com_dev_ops_t KeyboardDevOps = {.read      = keyboard_read,
                                       .poll_head = keyboard_poll_head,
                                       .poll      = keyboard_poll,
                                       .stat      = keyboard_stat};

int com_io_keyboard_send_packet(com_keyboard_t   *keyboard,
                                com_kbd_packet_t *pkt) {
    return kringbuffer_write(NULL,
                             &keyboard->rb,
                             pkt,
                             sizeof(com_kbd_packet_t),
                             sizeof(com_kbd_packet_t),
                             false,
                             keyboard_poll_callback,
                             keyboard,
                             NULL);
}

com_keyboard_t *com_io_keyboard_new(com_intf_kbd_layout_t layout) {
    size_t kbd_num = __atomic_fetch_add(&NextKeyboardNum, 1, __ATOMIC_SEQ_CST);
    KLOG("initializing keyboard in /dev/keyboard%u", kbd_num);

    // Poor man's sprintf
    char kbd_num_str[24];
    kuitoa(kbd_num, kbd_num_str);
    char kbd_name[8 + 24] = {0}; // "keyboard" + number
    kstrcpy(kbd_name, "keyboard");
    kstrcpy(&kbd_name[8], kbd_num_str);

    com_keyboard_t *keyboard = com_mm_slab_alloc(sizeof(com_keyboard_t));
    keyboard->layout         = DEFAULT_LAYOUT;
    KRINGBUFFER_INIT(&keyboard->rb);
    LIST_INIT(&keyboard->pollhead.polled_list);

    if (NULL != layout) {
        keyboard->layout = layout;
    }

    int devfs_ret = com_fs_devfs_register(&keyboard->vnode,
                                          NULL,
                                          kbd_name,
                                          kstrlen(kbd_name),
                                          &KeyboardDevOps,
                                          keyboard);
    if (0 != devfs_ret) {
        com_mm_slab_free(keyboard, sizeof(com_keyboard_t));
        return NULL;
    }

    return keyboard;
}
