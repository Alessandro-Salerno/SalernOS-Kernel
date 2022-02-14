#include "Syscall/dispatcher.h"
#include "Syscall/syscalls.h"
#include "User/Output/Text/kernelshell.h"


void kernel_syscall_dispatch(void* __frame, int __syscall) {
    kernel_shell_printf("\n\nSYSTEM CALL INFORMATION:\nFrame Poiinter: %u\nSyscall ID: %d\n", (uint64_t)(__frame), __syscall);

    switch (__syscall) {
        case 1: kernel_syscall_handlers_printstr(__frame);
        default: break;
    }
}
