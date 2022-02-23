#ifndef SALERNOS_CORE_KERNEL_PANIC
#define SALERNOS_CORE_KERNEL_PANIC

    #include <kerntypes.h>

    #define SOFTASSERT(__cond, __ret) if (!(__cond)) return __ret;


    typedef struct KernelPanicInfo {
        const char* _Type;
        const char* _Fault;
        const char* _Message;
    } panicinfo_t;


    /*******************************************************************************
    RET TYPE        FUNCTION NAME           FUNCTION ARGUMENTS
    *******************************************************************************/
    void            kernel_panic_throw      (const char* __message);
    void            kernel_panic_format     (panicinfo_t __info);
    void            kernel_panic_assert     (uint8_t __cond, const char* __message);

#endif
