#ifndef SALERNOS_CORE_KERNEL_BMP
#define SALERNOS_CORE_KERNEL_BMP

    #include <kerntypes.h>


    typedef struct Bitmap {
        size_t   _Size;
        uint8_t* _Buffer;
    } bmp_t;

    
    /********************************************************************************
    RET TYPE        FUNCTION NAME       FUNCTION ARGUMENTS
    ********************************************************************************/
    bool_t          kernel_bmp_get      (bmp_t* __bmp, uint64_t __idx);
    void            kernel_bmp_set      (bmp_t* __bmp, uint64_t __idx, bool_t __val);

#endif
