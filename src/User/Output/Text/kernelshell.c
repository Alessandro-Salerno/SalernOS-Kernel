#include "User/Output/Text/kernelshell.h"
#include "user/Output/Text/textrenderer.h"
#include <kstr.h>
#include <stdarg.h>


void kernel_shell_printf(const char* __fmt, ...) {
    char* _ptr = (char*)(__fmt);

    va_list _arguments;
    va_start(_arguments, __fmt);

    while (*_ptr != 0) {
        if (*_ptr == '%' && *(_ptr + 1) != ' ') {
            switch (*(_ptr + 1)) {
                case 'u': { kernel_text_print_str((char*)(uitoa(va_arg(_arguments, uint64_t)))); } break;
                
                case 'd':
                case 'i': { kernel_text_print_str((char*)(itoa(va_arg(_arguments, int64_t))));   } break;

                case 'c': { kernel_text_print_char((char)(va_arg(_arguments, signed)));                       } break;
            }

            _ptr++;
        }
        
        else
        kernel_text_print_char(*_ptr);

        _ptr++;
    }

    va_end(_arguments);
}
