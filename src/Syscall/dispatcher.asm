[bits 64]
kernel_syscall_invoke:
    int 0x81

GLOBAL kernel_syscall_invoke
