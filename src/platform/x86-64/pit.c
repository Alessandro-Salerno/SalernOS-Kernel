#include <kernel/com/util.h>
#include <kernel/platform/x86-64/io.h>
#include <kernel/platform/x86-64/pit.h>

static u16 pit_read(void) {
    io_outb(0x43, 0);
    u8 low = io_inb(0x40);
    u8 high = io_inb(0x40);
    return low | high << 8;
}

u64 pit_wait_ticks(u64 ticks) {
    io_outb(0x43, 0x34);
    io_outb(0x40, 0xff);
    io_outb(0x40, 0xff);

    u64 elapsed_ticks = 0;
    u16 prev = 0xffff - pit_read();

    while (elapsed_ticks < ticks) {
        u16 now = 0xffff - pit_read();
        u16 delta = now - prev;
        elapsed_ticks += delta;
        prev = now;
    }

    return elapsed_ticks;
}
