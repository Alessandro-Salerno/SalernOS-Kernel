#include "Syscall/syscalls.h"
#include "User/Output/Text/kernelshell.h"


void kernel_syscall_handlers_printstr(void* __frame) {
    char* _str = __frame;
    kernel_shell_printf(_str);
}
