#ifndef SALERNOS_CORE_KERNEL_PAGE_FRAME_ALLOCATOR
#define SALERNOS_CORE_KERNEL_PAGE_FRAME_ALLOCATOR

    #include "kerntypes.h"
    #include "Memory/mmap.h"
    #include "Memory/bmp.h"


    /***********************************************************************************************************************
    RET TYPE        FUNCTION NAME                       FUNCTION ARGUMENTS
    ***********************************************************************************************************************/
    void            kernel_allocator_initialize         ();
    void            kernel_allocator_free_page          (void* __address);
    void            kernel_allocator_free_pages         (void* __address, uint64_t __pagecount);
    void            kernel_allocator_lock_page          (void* __address);
    void            kernel_allocator_lock_pages         (void* __address, uint64_t __pagecount);

    void            kernel_allocator_get_infO           (uint64_t* __freemem, uint64_t* __usedmem, uint64_t* __reservedmem);
    void*           kernel_allocator_allocate_page      ();

#endif
