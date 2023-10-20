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


#include "user/Output/Text/textrenderer.h"
#include <kstdio.h>


#include <stdarg.h>
#include <kstr.h>


void kprintf(const char* __fmt, ...) {
    va_list _arguments;
    va_start(_arguments, __fmt);

    for (char* _ptr = (char*)(__fmt); *_ptr; _ptr++) {
        switch (*_ptr) {
            case '%': {
                switch (*(++_ptr)) {
                    case 'u'    :   kernel_text_print((char*)(uitoa(va_arg(_arguments, uint64_t))));  break;
                    
                    case 'd'    :
                    case 'i'    :   kernel_text_print((char*)(itoa(va_arg(_arguments, int64_t))));    break;
                    
                    case 'c'    :   kernel_text_putch((char)(va_arg(_arguments, signed)));            break;

                    case 's'    :   kernel_text_print((char*)(va_arg(_arguments, char*)));            break;
                }
                
                break;
            }

            default:
                kernel_text_putch(*_ptr);
                break;
        }
    }

    va_end(_arguments);
}
