#ifndef SALERNOS_CORE_KERNEL_PAGE_FRAME_ALLOCATOR
#define SALERNOS_CORE_KERNEL_PAGE_FRAME_ALLOCATOR

    #include "Memory/mmap.h"
    #include "Memory/bmp.h"
    #include <kerntypes.h>


    /***********************************************************************************************************************
    RET TYPE        FUNCTION NAME                       FUNCTION ARGUMENTS
    ***********************************************************************************************************************/
    void            kernel_allocator_initialize         ();
    void            kernel_allocator_free               (void* __address, uint64_t __pagecount);
    void            kernel_allocator_lock               (void* __address, uint64_t __pagecount);
    void            kernel_allocator_reserve            (void* __address, uint64_t __pagecount);
    void            kernel_allocator_unreserve          (void* __address, uint64_t __pagecount);

    void            kernel_allocator_info_get           (uint64_t* __freemem, uint64_t* __usedmem, uint64_t* __reservedmem);
    void*           kernel_allocator_page_new           ();

#endif
