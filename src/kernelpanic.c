/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2022 Alessandro Salerno

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**********************************************************************/


#include "User/Output/Text/textrenderer.h"
#include "kernelpanic.h"
#include <kstdio.h>
#include <kdebug.h>


void kernel_panic_throw(const char* __message, intframe_t* __regstate) {
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
        kprintf("R09: %u\nR08: %u\n\n", __regstate->_R9, __regstate->_R8);

        kprintf("[Special Registers]:\n");
        kprintf("RIP: %u\nRSP: %u\n", __regstate->_RIP, __regstate->_RSP);
        kprintf("CS: %u\nSS: %u\n",   __regstate->_CS, __regstate->_SS);
        kprintf("RFlags: %u\n",       __regstate->_RFlags);
    }

    while (TRUE);
}

void kernel_panic_assert(uint8_t __cond, const char* __message) {
    SOFTASSERT(!(__cond), RETVOID);
    kernel_panic_throw(__message, NULL);
}
