/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2023 Alessandro Salerno

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

#pragma once

// System includes
#include "sys/gdt/gdt.h" /*   Includes the Kernel's definition of the GTD        */
#include "sys/interrupts/handlers.h" /*   Includes the Kernel's Interrupt Service Routines   */
#include "sys/interrupts/idt.h" /*   Includes the Kernel's definition of the IDT        */
#include "sys/interrupts/pic.h" /*   Includes the basic Kernel PIC Driver               */
#include "sys/legacy-io/io.h" /*   Includes the basic Kernel I/O functions            */

// Hardware includes
#include "acpi/acpi.h" /*   Includes the Kernel's RSDP/ACPI Implementation     */
#include "dev/drivers/ahci/ahci.h" /*   Includes the Kernel's AHCI driver                  */
#include "dev/pci/pci.h" /*   Includes the Kernel's PCI Implementation           */

// Syscall includes
#include "sys/syscall/dispatcher.h" /*   Includes the Kernel Syscall Dispatcher functions   */
#include "sys/syscall/sce.h"
#include "sys/syscall/syscalls.h" /*   Includes all kernel Syscall declarations           */

// Memory includes
#include "mm/Heap/heap.h" /*   Includes the Kernel's heap manager                 */
#include "mm/bmp.h"  /*   Includes the Kernel's definition of a Bitmap       */
#include "mm/mmap.h" /*   Includes the Kernel EFI Memory Map Interface       */
#include "mm/paging.h" /*   Includes the Kernel Page Table Manager             */
#include "mm/pgfalloc.h" /*   Includes the Kernel Page Frame Allocator           */

// Time includes
#include "time/pit/pit.h" /*   Includes the Kernel's PIT driver                   */

// Utils includes
#include "kernelpanic.h" /*   Includes the Kernel panic handler                  */
#include "kutils.h" /*   Includes all kernel initialization functions       */

// Kernel Library Includes
#include <kmalloc.h>
#include <kstdio.h>

// Other includes
#include <limine.h>
#include <sbs.h>
