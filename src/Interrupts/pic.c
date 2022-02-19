#include "Interrupts/pic.h"
#include "IO/io.h"


void kernel_interrupts_pic_remap() {
    asm volatile ("cli");

    uint8_t _pic1 = kernel_io_in_wait(PIC1_DATA),
            _pic2 = kernel_io_in_wait(PIC2_DATA);

    kernel_io_out_wait(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    kernel_io_out_wait(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    kernel_io_out_wait(PIC1_DATA, 0x20);\
    kernel_io_out_wait(PIC2_DATA, 0x28);
    kernel_io_out_wait(PIC1_DATA, 0x04);
    kernel_io_out_wait(PIC2_DATA, 0x02);
    kernel_io_out_wait(PIC1_DATA, ICW4_8086);
    kernel_io_out_wait(PIC2_DATA, ICW4_8086);

    kernel_io_out(PIC1_DATA, _pic1);
    kernel_io_out(PIC2_DATA, _pic2);

    kernel_io_out(PIC1_DATA, 0b11111101);
    kernel_io_out(PIC2_DATA, 0b11111111);
    asm volatile ("sti");
}

void kernel_interrupts_pic_master_end() {
    kernel_io_out(PIC1_COMMAND, PIC_EOI);
}

void kernel_interrupts_pic_slave_end() {
    kernel_io_out(PIC2_COMMAND, PIC_EOI);
    kernel_io_out(PIC1_COMMAND, PIC_EOI);
}
