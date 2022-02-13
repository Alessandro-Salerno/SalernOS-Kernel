#ifndef SALERNOS_CORE_KERNEL_PIC
#define SALERNOS_CORE_KERNEL_PIC

    #include "kerntypes.h"

    #define PIC1_COMMAND 0x20
    #define PIC2_COMMAND 0xA0
    #define PIC1_DATA    0x21
    #define PIC2_DATA    0xA1
    #define PIC_EOI      0x20

    #define ICW1_INIT 0x10
    #define ICW1_ICW4 0x01
    #define ICW4_8086 0x01


    /**********************************************************************
    RET TYPE        FUNCTION NAME                       FUNCTION ARGUMENTS
    **********************************************************************/
    void            kernel_interrupts_pic_remap         ();
    void            kernel_interrupts_pic_end_master    ();
    void            kernel_interrupts_pic_end_slave     ();

#endif
