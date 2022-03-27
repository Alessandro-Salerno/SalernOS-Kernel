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


#ifndef SALERNOS_INC_KERNEL_INCLUDES
#define SALERNOS_INC_KERNEL_INCLUDES

    // User Output includes
    #include "User/Output/Text/textrenderer.h"      /*   Includes the basic Kernel Text Renderer            */
    #include "User/Output/Display/kdd.h"            /*   Includes the basic Kernel Display Driver           */

    // User Input includes
    #include "User/Input/Keyboard/keyboard.h"       /*   Includes the basic Kernel Keyboard Driver          */

    // System includes
    #include "Interrupts/handlers.h"                /*   Includes the Kernel's Interrupt Service Routines   */
    #include "Interrupts/idt.h"                     /*   Includes the Kernel's definition of the IDT        */
    #include "Interrupts/pic.h"                     /*   Includes the basic Kernel PIC Driver               */
    #include "GDT/gdt.h"                            /*   Includes the Kernel's definition of the GTD        */
    #include "IO/io.h"                              /*   Includes the basic Kernel I/O functions            */

    // Hardware includes
    #include "Hardware/ACPI/acpi.h"                 /*   Includes the Kernel's RSDP/ACPI Implementation     */
    #include "Hardware/PCI/pci.h"                   /*   Includes the Kernel's PCI Implementation           */

    // Syscall includes
    #include "Syscall/dispatcher.h"                 /*   Includes the Kernel Syscall Dispatcher functions   */
    #include "Syscall/syscalls.h"                   /*   Includes all kernel Syscall declarations           */

    // Memory includes
    #include "Memory/Heap/heap.h"                   /*   Includes the Kernel's heap manager                 */
    #include "Memory/pgfalloc.h"                    /*   Includes the Kernel Page Frame Allocator           */
    #include "Memory/paging.h"                      /*   Includes the Kernel Page Table Manager             */
    #include "Memory/mmap.h"                        /*   Includes the Kernel EFI Memory Map Interface       */
    #include "Memory/bmp.h"                         /*   Includes the Kernel's definition of a Bitmap       */

    // Time includes
    #include "Time/PIT/pit.h"                       /*   Includes the Kernel's PIT driver                   */

    // Utils includes
    #include "kernelpanic.h"                        /*   Includes the Kernel panic handler                  */
    #include "kutils.h"                             /*   Includes all kernel initialization functions       */

    // Kernel Library Includes
    #include <kmalloc.h>
    #include <kstdio.h>

    // Other includes
    #include <sbs.h>

#endif
