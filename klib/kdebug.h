#ifndef SALERNOS_LIB_KERNEL_DEBUG
#define SALERNOS_LIB_KERNEL_DEBUG

    #include <kerntypes.h>


    /***********************************************************
    RET TYPE        FUNCTION NAME           FUNCTION ARGUMENTS
    ***********************************************************/
    void            kloginfo                (const char* __msg);
    void            klogerr                 (const char* __msg);
    void            klogwarn                (const char* __msg);
    void            klogok                  (const char* __msg);

#endif
