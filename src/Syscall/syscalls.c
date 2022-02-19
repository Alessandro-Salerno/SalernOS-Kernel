#include "Syscall/syscalls.h"
#include <kstdio.h>


void kernel_syscall_handlers_printstr(void* __frame) {
    char* _str = __frame;
    kprintf(_str);
}
