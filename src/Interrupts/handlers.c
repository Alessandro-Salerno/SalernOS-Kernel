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


#include "User/Input/Keyboard/keyboard.h"
#include "Interrupts/handlers.h"
#include "Interrupts/pic.h"
#include "kernelpanic.h"
#include "IO/io.h"


void kernel_interrupt_handlers_pgfault(intframe_t* __frame)     { kernel_panic_throw("Page Fault Detected!");               }
void kernel_interrupt_handlers_dfault(intframe_t* __frame)      { kernel_panic_throw("Double Fault Detected!");             }
void kernel_interrupt_handlers_gpfault(intframe_t* __frame)     { kernel_panic_throw("General Protection Fault Detected!"); }

void kernel_interrupt_handlers_kbhit(intframe_t* __frame) {
    uint64_t _scode = kernel_io_in(0x60);

    kernel_io_keyboard_mods_handle(_scode);
    kernel_interrupts_pic_master_end();
}
