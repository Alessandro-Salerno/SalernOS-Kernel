#include "Interrupts/idt.h"
#include "Interrupts/handlers.h"
#include "Memory/palloc.h"
#include "kernelpanic.h"
#include "Syscall/dispatcher.h"

#define IDT_DINIT "Interrupt Descriptor Table Fault:\nKernel tried to reinitialize IDT."


static idtr_t idtr;
static bool_t idtInitialized;


void kernel_idt_initialize() {
    kernel_panic_assert(!idtInitialized, IDT_DINIT);

    idtr._Limit  = 0x0fff;
    idtr._Offset = (uint64_t)(kernel_allocator_allocate_page()); 

    idtdescent_t* _pgfault_handler = (idtdescent_t*)(idtr._Offset + 0xE * sizeof(idtdescent_t));
    kernel_idt_set_offset(_pgfault_handler, (uint64_t)(kernel_interrupt_handlers_pgfault));
    _pgfault_handler->_TypesAndAttributes = IDT_TA_INTERRUPT_GATE;
    _pgfault_handler->_Selector           = 0x08;

    idtdescent_t* _dfault_handler = (idtdescent_t*)(idtr._Offset + 0x8 * sizeof(idtdescent_t));
    kernel_idt_set_offset(_dfault_handler, (uint64_t)(kernel_interrupt_handlers_dfault));
    _dfault_handler->_TypesAndAttributes = IDT_TA_INTERRUPT_GATE;
    _dfault_handler->_Selector           = 0x08;

    idtdescent_t* _gpfault_handler = (idtdescent_t*)(idtr._Offset + 0xD * sizeof(idtdescent_t));
    kernel_idt_set_offset(_gpfault_handler, (uint64_t)(kernel_interrupt_handlers_gpfault));
    _gpfault_handler->_TypesAndAttributes = IDT_TA_INTERRUPT_GATE;
    _gpfault_handler->_Selector           = 0x08;

    idtdescent_t* _kbhit_handler = (idtdescent_t*)(idtr._Offset + 0x21 * sizeof(idtdescent_t));
    kernel_idt_set_offset(_kbhit_handler, (uint64_t)(kernel_interrupt_handlers_kbhit));
    _kbhit_handler->_TypesAndAttributes = IDT_TA_INTERRUPT_GATE;
    _kbhit_handler->_Selector           = 0x08;

    idtdescent_t* _syscall_handler = (idtdescent_t*)(idtr._Offset + 0x81 * sizeof(idtdescent_t));
    kernel_idt_set_offset(_syscall_handler, (uint64_t)(kernel_syscall_dispatch));
    _syscall_handler->_TypesAndAttributes = IDT_TA_INTERRUPT_GATE;
    _syscall_handler->_Selector           = 0x08;

    asm ("lidt %0" : : "m" (idtr));

    idtInitialized = 1;
}

void kernel_idt_set_offset(idtdescent_t* __ent, uint64_t __offset) {
    __ent->_OffsetZero = (uint16_t)(__offset  & 0x000000000000ffff);
    __ent->_OffsetOne  = (uint16_t)((__offset & 0x00000000ffff0000) >> 16);
    __ent->_OffsetTwo  = (uint32_t)((__offset & 0xffffffff00000000) >> 32);
}

uint64_t kernel_idt_get_offset(idtdescent_t* __ent) {
    return (uint64_t) (
        (uint64_t)(__ent->_OffsetZero)      |
        (uint64_t)(__ent->_OffsetOne) << 16 |
        (uint64_t)(__ent->_OffsetTwo) << 32
    );
}
