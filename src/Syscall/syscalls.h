#ifndef SALERNOS_CORE_KERNEL_SYSCALLS
#define SALERNOS_CORE_KERNEL_SYSCALLS

    #include "kerntypes.h"

    #define SYSCALL_PRINT_STR 0


    /***********************************************************************
    RET TYPE        FUNCTION NAME                        FUNCTION ARGUMENTS
    ***********************************************************************/
    void            kernel_syscall_handlers_printstr    (void* __frame);

#endif
