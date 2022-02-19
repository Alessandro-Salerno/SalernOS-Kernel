#include "Syscall/dispatcher.h"
#include "Syscall/syscalls.h"
#include "kernelpanic.h"
#include <kstdio.h>


static const void (*syscallHandlers[])(void* __frmae) = {
    kernel_syscall_handlers_printstr
};


void kernel_syscall_dispatch(void* __frame, int __syscall) {
    kernel_panic_assert(
        __syscall < sizeof(syscallHandlers) / sizeof(syscallHandlers[0]),
        "Syscall invoked with invalid ID!"
    );

    kprintf("\n\nSYSTEM CALL INFORMATION:\nFrame Poiinter: %u\nSyscall ID: %d\n", (uint64_t)(__frame), __syscall);
    syscallHandlers[__syscall](__frame);
}
