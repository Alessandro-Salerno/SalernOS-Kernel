#ifndef SALERNOS_CORE_KERNEL_PAGE_FRAME_ALLOCATOR
#define SALERNOS_CORE_KERNEL_PAGE_FRAME_ALLOCATOR

    #include "Memory/mmap.h"
    #include "Memory/bmp.h"
    #include <kerntypes.h>


    /***********************************************************************************************************************
    RET TYPE        FUNCTION NAME                  FUNCTION ARGUMENTS
    ***********************************************************************************************************************/
    void            kernel_pgfa_initialize         ();
    void            kernel_pgfa_free               (void* __address, uint64_t __pagecount);
    void            kernel_pgfa_lock               (void* __address, uint64_t __pagecount);
    void            kernel_pgfa_unreserve          (void* __address, uint64_t __pagecount);
    void            kernel_pgfa_reserve            (void* __address, uint64_t __pagecount);

    void            kernel_pgfa_info_get           (uint64_t* __freemem, uint64_t* __usedmem, uint64_t* __reservedmem);
    void*           kernel_pgfa_page_new           ();

#endif
