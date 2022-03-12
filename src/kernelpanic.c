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


void kernel_panic_throw(const char* __message) {
    klogerr("Kernel panic: execution of the kernel has been halted due to a critical system fault.");
    kprintf("\n\n%s\n", __message);

    while (1);
}

void kernel_panic_assert(uint8_t __cond, const char* __message) {
    SOFTASSERT(!(__cond), RETVOID);
    kernel_panic_throw(__message);
}
