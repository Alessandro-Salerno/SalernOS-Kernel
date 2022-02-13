#ifndef SALERNOS_CORE_KERNEL_SYSCALL_DISPATCHER
#define SALERNOS_CORE_KERNEL_SYSCALL_DISPATCHER

    #include "kerntypes.h"


    /****************************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    ****************************************************************************/
    void            kernel_syscall_invoke         (void* __frame, int __syscall);
    void            kernel_syscall_dispatch       (void* __frame, int __syscall);

#endif
