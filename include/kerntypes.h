#ifndef SALERNOS_INC_KERNEL_TYPES
#define SALERNOS_INC_KERNEL_TYPES

    #include <stddef.h>
    #include <stdint.h>

    #define RETVOID
    #define ARGRET(__arg, __val) if (__arg != NULL) *__arg = __val
    
    typedef uint8_t bool_t;

#endif
