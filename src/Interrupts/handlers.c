#include "Interrupts/handlers.h"
#include "Interrupts/pic.h"
#include "User/Output/Text/kernelshell.h"
#include "kernelpanic.h"
#include "User/Input/Keyboard/keyboard.h"
#include "io.h"


void kernel_interrupt_handlers_pgfault(struct InterruptFrame* __frame) {
    kernel_panic_throw("Page Fault Detected!");
}

void kernel_interrupt_handlers_dfault(struct InterruptFrame* __frame) {
    kernel_panic_throw("Double Fault Detected!");
}

void kernel_interrupt_handlers_gpfault(struct InterruptFrame* __frame) {
    kernel_panic_throw("General Protection Fault Detected!");
}

void kernel_interrupt_handlers_kbhit(struct InterruptFrame* __frame) {
    uint64_t _scode = kernel_io_in(0x60);

    kernel_io_keyboard_handle_mods(_scode);
    kernel_interrupts_pic_end_master();
}
