#include "user/Output/Text/textrenderer.h"
#include "kstdio.h"
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
