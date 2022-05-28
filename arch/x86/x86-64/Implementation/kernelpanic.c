#include "Interrupts/x86_64-handlers.h"
#include <kernelpanic.h>
#include <kstdio.h>
#include <kdebug.h>


void arch_panic_throw(const char* __message, intframe_t* __regstate) {
    klogerr("Kernel panic: execution of the kernel has been halted due to a critical system fault.");
    kprintf("%s\n\n", __message);

    if (__regstate != NULL) {
        kprintf("[Registers]:\n");
        kprintf("RBP: %u\nRDI: %u\n", __regstate->_RBP, __regstate->_RDI);
        kprintf("RSI: %u\nRDX: %u\n", __regstate->_RSI, __regstate->_RDX);
        kprintf("RCX: %u\nRBX: %u\n", __regstate->_RCX, __regstate->_RBX);
        kprintf("RAX: %u\n\n",        __regstate->_RAX);

        kprintf("[Numbered Registers]:\n");
        kprintf("R15: %u\nR14: %u\n",   __regstate->_R15, __regstate->_R14);
        kprintf("R13: %u\nR12: %u\n",   __regstate->_R13, __regstate->_R12);
        kprintf("R11: %u\nR10: %u\n",   __regstate->_R11, __regstate->_R10);
        kprintf("R09: %u\nR08: %u\n\n", __regstate->_R9,  __regstate->_R8);

        kprintf("[Special Registers]:\n");
        kprintf("RIP: %u\nRSP: %u\n", __regstate->_RIP, __regstate->_RSP);
        kprintf("CS: %u\nSS: %u\n",   __regstate->_CS,  __regstate->_SS);
        kprintf("RFlags: %u\n",       __regstate->_RFlags);
    }

    while (TRUE);
}