#ifndef SALERNOS_CORE_KERNEL_IDT
#define SALERNOS_CORE_KERNEL_IDT

    #include <kerntypes.h>

    #define IDT_TA_INTERRUPT_GATE 0b10001110
    #define IDT_TA_CALL_GATE      0b10001100
    #define IDT_TA_TRAP_GATE      0b10001111


    typedef struct __attribute__((__packed__)) IDTDescriptorEntry {
        uint16_t _OffsetZero;
        uint16_t _Selector;
        uint8_t  _InterruptStackTableOffset;
        uint8_t  _TypesAndAttributes;
        uint16_t _OffsetOne;
        uint32_t _OffsetTwo;
        uint32_t _Ignore;
    } __attribute__((__packed__)) idtdescent_t;

    typedef struct __attribute__((__packed__)) InterruptDescriptorTableRegister {
        uint16_t _Limit;
        uint64_t _Offset;
    } __attribute__((__packed__)) idtr_t;

    /**************************************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    **************************************************************************************/
    void            kernel_idt_initialize         ();
    void            kernel_idt_offset_set         (idtdescent_t* __ent, uint64_t __offset);
    uint64_t        kernel_idt_offset_get         (idtdescent_t* __ent);

#endif
