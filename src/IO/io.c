#include "IO/io.h"


void kernel_io_out(uint16_t __port, uint8_t __val) {
    asm volatile ("outb %0, %1" : : "a" (__val), "Nd" (__port));
}

uint8_t kernel_io_in(uint16_t __port) {
    uint8_t _retval;
    asm volatile ("inb %1, %0" : "=a" (_retval) : "Nd" (__port));
    
    return _retval;
}

void kernel_io_wait() {
    asm volatile ("outb %%al, $0x80" : : "a" (0));
}
