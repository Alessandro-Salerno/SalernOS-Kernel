#include <errno.h>
#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/crt/usercopy.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/io/mouse.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <poll.h>

static usize g_next_mouse_num = 0;

static void mouse_poll_callback(void *arg) {
    Mouse *mouse = arg;

    spinlock_acq(&mouse->pollhead.lock);
    Polled *polled, *_;
    LIST_FOREACH_SAFE(polled, &mouse->pollhead.polled_list, polled_list, _) {
        poll_notify(polled->poller);
    }
    spinlock_rel(&mouse->pollhead.lock);
}

static int mouse_read(void *buf, usize buflen, void *devdata, u64 off,
                      u64 flags, usize *out_bytes_read) {
    (void)off;
    Mouse *mouse = devdata;
    int ret = 0;
    // TODO: sanitize buflen and make sure it's a multiple of the packet size,
    // otherwise we leave garbage in the rb
    sync_acq(&mouse->rb.condvar);

    if (ringbuffer_avail_read_nolock(&mouse->rb) == 0 &&
        mouse->consolidated.present) {
        usize copy_size = min(buflen, sizeof(MousePacket));
        ret = memcpy_maybe_user(buf, &mouse->consolidated.packet, copy_size);
        if (ret != 0) {
            *out_bytes_read = 0;
            goto end;
        }
        memset(&mouse->consolidated.packet, 0, sizeof(MousePacket));
        mouse->consolidated.present = false;
        *out_bytes_read = copy_size;
        goto end;
    }

    ret = ringbuffer_read_nolock(buf, &mouse->rb, buflen, sizeof(MousePacket),
                                 !(flags & O_NONBLOCK), mouse_poll_callback,
                                 mouse, NULL, out_bytes_read);

end:
    sync_rel(&mouse->rb.condvar);
    return ret;
}

static int mouse_poll_head(void *devdata, PollHead **out_poll_head) {
    Mouse *mouse = devdata;
    *out_poll_head = &mouse->pollhead;
    return 0;
}

static int mouse_poll(void *devdata, short events, short *out_revents) {
    (void)events;
    Mouse *mouse = devdata;
    short out = 0;

    sync_acq(&mouse->rb.condvar);
    if (ringbuffer_avail_read_nolock(&mouse->rb) > 0 ||
        mouse->consolidated.present) {
        out |= POLLIN;
    }
    sync_rel(&mouse->rb.condvar);

    *out_revents = out;
    return 0;
}

static int mouse_stat(void *devdata, struct stat *out_stat) {
    (void)devdata;
    out_stat->st_blksize = 512;
    out_stat->st_mode = 0666;
    out_stat->st_rdev = 10 << 8;
    return 0;
}

static DevOps g_mouse_dev_ops = {.type = VNODE_TYPE_CHARDEV,
                                 .read = mouse_read,
                                 .poll_head = mouse_poll_head,
                                 .poll = mouse_poll,
                                 .stat = mouse_stat};

// Packets reach userspace, so they must not carry kernel addresses
int mouse_send_packet(Mouse *mouse, MousePacket *pkt) {
    MousePacket upkt = *pkt;
    upkt.mouse = NULL;
    sync_acq(&mouse->rb.condvar);
    int rb_err = ringbuffer_write_nolock(
        &mouse->rb, &upkt, sizeof(MousePacket), sizeof(MousePacket), false,
        mouse_poll_callback, mouse, NULL, NULL);

    // If we received EAGAIN, this means that the packet could not be written to
    // the ringbuffer and thus we consolidate the values as explained in mouse.h
    if (rb_err == EAGAIN) {
        mouse->consolidated.packet.buttons |= pkt->buttons;
        mouse->consolidated.packet.dx += pkt->dx;
        mouse->consolidated.packet.dy += pkt->dy;
        mouse->consolidated.packet.dz += pkt->dz;
        mouse->consolidated.present = true;
        mouse_poll_callback(mouse);
        rb_err = 0;
    }

    sync_rel(&mouse->rb.condvar);
    return rb_err;
}

Mouse *mouse_new(void) {
    usize mouse_num = __atomic_fetch_add(&g_next_mouse_num, 1,
                                         __ATOMIC_SEQ_CST);
    log("initializing mouse in /dev/mouse%zu", mouse_num);

    // Poor man's sprintf
    char mouse_num_str[24];
    uitoa(mouse_num, mouse_num_str);
    char mouse_name[5 + 24] = {0}; // "mouse" + number
    strcpy(mouse_name, "mouse");
    strcpy(&mouse_name[5], mouse_num_str);

    Mouse *mouse = slab_alloc(sizeof(Mouse));
    ringbuffer_init(&mouse->rb);
    sync_init_mutex(&mouse->rb.condvar);
    LIST_INIT(&mouse->pollhead.polled_list);

    int devfs_ret = devfs_register(NULL, mouse_name, strlen(mouse_name),
                                   &g_mouse_dev_ops, mouse, &mouse->vnode);
    if (devfs_ret != 0) {
        slab_free(mouse, sizeof(Mouse));
        return NULL;
    }

    return mouse;
}
