#ifndef SALERNOS_CORE_KERNEL_INTERRUPT_HANDLERS
#define SALERNOS_CORE_KERNEL_INTERRUPT_HANDLERS

    #include <kerntypes.h>

    #define ISR __attribute__((interrupt))


    struct InterruptFrame;
    typedef struct InterruptFrame intframe_t;


    /*********************************************************************************
         RET TYPE        FUNCTION NAME                          FUNCTION ARGUMENTS
    *********************************************************************************/
    ISR  void            kernel_interrupt_handlers_pgfault      (intframe_t* __frame);
    ISR  void            kernel_interrupt_handlers_dfault       (intframe_t* __frame);
    ISR  void            kernel_interrupt_handlers_gpfault      (intframe_t* __frame);
    ISR  void            kernel_interrupt_handlers_kbhit        (intframe_t* __frame);

#endif
