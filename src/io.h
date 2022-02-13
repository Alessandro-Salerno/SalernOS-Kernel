#ifndef SALERNOS_CORE_KERNEL_IO
#define SALERNOS_CORE_KERNEL_IO

    #include "kerntypes.h"
    

    /**************************************************************************************************
    RET TYPE        FUNCTION NAME       FUNCTION ARGUMENTS
    **************************************************************************************************/
    void            kernel_io_out       (uint16_t __port, uint8_t __val);
    uint8_t         kernel_io_in        (uint16_t __port);
    void            kernel_io_wait      ();

#endif
