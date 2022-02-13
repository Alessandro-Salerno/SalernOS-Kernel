#ifndef SALERNOS_CORE_KERNEL_INTERRUPT_HANDLERS
#define SALERNOS_CORE_KERNEL_INTERRUPT_HANDLERS

    #include "kerntypes.h"

    #define ISR __attribute__((interrupt))


    struct InterruptFrame;
    typedef struct InterruptFrame intframe_t;


    /********************************************************************************************
         RET TYPE        FUNCTION NAME                          FUNCTION ARGUMENTS
    ********************************************************************************************/
    ISR  void            kernel_interrupt_handlers_pgfault      (struct InterruptFrame* __frame);
    ISR  void            kernel_interrupt_handlers_dfault       (struct InterruptFrame* __frame);
    ISR  void            kernel_interrupt_handlers_gpfault      (struct InterruptFrame* __frame);
    ISR  void            kernel_interrupt_handlers_kbhit        (struct InterruptFrame* __frame);

#endif
