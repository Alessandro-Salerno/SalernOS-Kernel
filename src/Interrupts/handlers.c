#include "Interrupts/handlers.h"
#include "Interrupts/pic.h"
#include "User/Output/Text/kernelshell.h"
#include "kernelpanic.h"
#include "User/Input/Keyboard/keyboard.h"
#include "IO/io.h"


void kernel_interrupt_handlers_pgfault(intframe_t* __frame) {
    kernel_panic_throw("Page Fault Detected!");
}

void kernel_interrupt_handlers_dfault(intframe_t* __frame) {
    kernel_panic_throw("Double Fault Detected!");
}

void kernel_interrupt_handlers_gpfault(intframe_t* __frame) {
    kernel_panic_throw("General Protection Fault Detected!");
}

void kernel_interrupt_handlers_kbhit(intframe_t* __frame) {
    uint64_t _scode = kernel_io_in(0x60);

    kernel_io_keyboard_handle_mods(_scode);
    kernel_interrupts_pic_end_master();
}
