#ifndef SALERNOS_INC_KERNEL_MEMORY
#define SALERNOS_INC_KERNEL_MEMORY

    #include "kerntypes.h"


    static void memset(void* __buff, uint64_t __buffsize, uint8_t __val) {
        __uint128_t _128bit_val = __val;

        if (__buffsize < 16) {
            for (uint8_t _i = 0; _i < __buffsize; _i++)
                *(uint8_t*)((uint64_t)(__buff) + _i) = __val;
            
            return;
        }

        if (__val != 0) {
            for (uint8_t _i = 8; _i < 128; _i += 8)
                _128bit_val |= (__uint128_t)(__val) << _i;
        }

        for (uint64_t _i = 0; _i < __buffsize; _i += 16)
            *(__uint128_t*)((uint64_t)(__buff) + _i) = _128bit_val;

        for (uint64_t _i = __buffsize - (__buffsize % 16); _i < __buffsize; _i++)
            *(uint8_t*)((uint64_t)(__buff) + _i) = _128bit_val;
    }

#endif
