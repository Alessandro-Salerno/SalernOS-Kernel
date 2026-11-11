#pragma once

#include <kernel/com/util.h>

static inline void io_outb(u16 port, u8 data) {
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

static inline u8 io_inb(u16 port) {
    u8 ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_outw(u16 port, u16 data) {
    asm volatile("out %%ax, %%dx" : : "a"(data), "d"(port));
}

static inline u16 io_inw(u16 port) {
    u16 ret;
    asm volatile("in %%dx, %%ax" : "=a"(ret) : "d"(port));
    return ret;
}

static inline void io_outd(u16 port, u32 data) {
    asm volatile("out %%eax, %%dx" : : "a"(data), "d"(port));
}

static inline u32 io_ind(u16 port) {
    u32 ret;
    asm volatile("in %%dx, %%eax" : "=a"(ret) : "d"(port));
    return ret;
}
