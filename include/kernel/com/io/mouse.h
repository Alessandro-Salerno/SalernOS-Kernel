#pragma once

#include <kernel/com/ds/ringbuffer.h>
#include <kernel/com/fs/poll.h>
#include <kernel/com/fs/vfs.h>

#define MOUSE_RIGHTBUTTON 0x01
#define MOUSE_MIDBUTTON   0x02
#define MOUSE_LEFTBUTTON  0x04
#define MOUSE_BUTTON4     0x08
#define MOUSE_BUTTON5     0x10

typedef struct MousePacket {
    struct Mouse *mouse;
    int buttons;
    int dx;
    int dy;
    int dz;
} MousePacket;

typedef struct Mouse {
    Vnode *vnode;
    Ringbuffer rb;
    struct PollHead pollhead;

    // Packets are first written to the ringbuffer, but since that is only
    // PAGE_SIZE / 4, it will fill up very quickly. Thus, all other packets
    // are "consolidated" into a single one. The reason why this is not the
    // default (which would be much faster in terms of overhead) is that this
    // would apparently break mouse acceleration in userspace. Thus we try to
    // strike a balance between performance, precision, and features
    struct {
        MousePacket packet;
        bool present;
    } consolidated;
} Mouse;

Mouse *mouse_new(void);
int mouse_send_packet(Mouse *mouse, MousePacket *pkt);
