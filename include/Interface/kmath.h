#ifndef SALERNOS_INC_KERNEL_MATH
#define SALERNOS_INC_KERNEL_MATH

    #include <kerntypes.h>


    uint64_t kabs(int64_t __val) {
        return (__val < 0) ? __val * -1 : __val;
    }

#endif
