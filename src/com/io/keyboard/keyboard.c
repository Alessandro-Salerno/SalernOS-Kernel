#include <fcntl.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/fs/devfs.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/io/keyboard.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sys/sched.h>
#include <kernel/com/util.h>
#include <poll.h>

#define __DEFAULT_LAYOUT(name) keyboard_layout_##name
#define _DEFAULT_LAYOUT(name)  __DEFAULT_LAYOUT(name)
#define DEFAULT_LAYOUT         _DEFAULT_LAYOUT(CONFIG_DEFAULT_KBD_LAYOUT)

static usize g_next_keyboard_num = 0;

static void keyboard_poll_callback(void *arg) {
    Keyboard *keyboard = arg;

    spinlock_acq(&keyboard->pollhead.lock);
    Polled *polled, *_;
    LIST_FOREACH_SAFE(polled, &keyboard->pollhead.polled_list, polled_list, _) {
        poll_notify(polled->poller);
    }
    spinlock_rel(&keyboard->pollhead.lock);
}

static int keyboard_read(void *buf, usize buflen, void *devdata, u64 off,
                         u64 flags, usize *out_bytes_read) {
    (void)off;
    Keyboard *keyboard = devdata;
    return ringbuffer_read(buf, &keyboard->rb, buflen, sizeof(KbdPacket),
                           !(flags & O_NONBLOCK), keyboard_poll_callback,
                           keyboard, NULL, out_bytes_read);
}

static int keyboard_poll_head(void *devdata, PollHead **out_poll_head) {
    Keyboard *keyboard = devdata;
    *out_poll_head = &keyboard->pollhead;
    return 0;
}

static int keyboard_poll(void *devdata, short events, short *out_revents) {
    (void)events;
    Keyboard *keyboard = devdata;
    *out_revents = ringbuffer_poll(&keyboard->rb) & POLLIN;
    return 0;
}

static int keyboard_stat(void *devdata, struct stat *out_stat) {
    (void)devdata;
    out_stat->st_blksize = 512;
    out_stat->st_mode = 0666;
    out_stat->st_rdev = (10 << 8);
    return 0;
}

static DevOps g_keyboard_dev_ops = {.type = VNODE_TYPE_CHARDEV,
                                    .read = keyboard_read,
                                    .poll_head = keyboard_poll_head,
                                    .poll = keyboard_poll,
                                    .stat = keyboard_stat};

// Packets reach userspace, so they must not carry kernel addresses
int keyboard_send_packet(Keyboard *keyboard, KbdPacket *pkt) {
    KbdPacket upkt = *pkt;
    upkt.keyboard = NULL;
    return ringbuffer_write(&keyboard->rb, &upkt, sizeof(KbdPacket),
                            sizeof(KbdPacket), false, keyboard_poll_callback,
                            keyboard, NULL, NULL);
}

Keyboard *keyboard_new(KbdLayoutFn layout) {
    usize kbd_num = __atomic_fetch_add(&g_next_keyboard_num, 1,
                                       __ATOMIC_SEQ_CST);
    log("initializing keyboard in /dev/keyboard%zu", kbd_num);

    // Poor man's sprintf
    char kbd_num_str[24];
    uitoa(kbd_num, kbd_num_str);
    char kbd_name[8 + 24] = {0}; // "keyboard" + number
    strcpy(kbd_name, "keyboard");
    strcpy(&kbd_name[8], kbd_num_str);

    Keyboard *keyboard = slab_alloc(sizeof(Keyboard));
    keyboard->layout = DEFAULT_LAYOUT;
    ringbuffer_init(&keyboard->rb);
    sync_init_mutex(&keyboard->rb.condvar);
    LIST_INIT(&keyboard->pollhead.polled_list);

    if (layout != NULL) keyboard->layout = layout;

    int devfs_ret = devfs_register(NULL, kbd_name, strlen(kbd_name),
                                   &g_keyboard_dev_ops, keyboard,
                                   &keyboard->vnode);
    if (devfs_ret != 0) {
        slab_free(keyboard, sizeof(Keyboard));
        return NULL;
    }

    return keyboard;
}

void keyboard_update_state(KbdPacket *pkt) {
    Keyboard *keyboard = pkt->keyboard;
    bool pressed = pkt->keystate == KEYBOARD_KEYSTATE_PRESSED;
    int mod = 0;

    switch (pkt->keycode) {
        case KEYBOARD_KEY_CAPSLOCK:
            keyboard->state.caps_lock ^= pressed;
            return;
        case KEYBOARD_KEY_LEFTCTRL:
            mod = KEYBOARD_MOD_LCTRL;
            break;
        case KEYBOARD_KEY_RIGHTCTRL:
            mod = KEYBOARD_MOD_RCTRL;
            break;
        case KEYBOARD_KEY_LEFTSHIFT:
            mod = KEYBOARD_MOD_LSHIFT;
            break;
        case KEYBOARD_KEY_RIGHTSHIFT:
            mod = KEYBOARD_MOD_RSHIFT;
            break;
        case KEYBOARD_KEY_LEFTALT:
            mod = KEYBOARD_MOD_LALT;
            break;
        case KEYBOARD_KEY_RIGHTALT:
            mod = KEYBOARD_MOD_RALT;
            break;
        default:
            return;
    }

    if (pressed) keyboard->state.mod |= mod;
    else keyboard->state.mod &= ~mod;
}
