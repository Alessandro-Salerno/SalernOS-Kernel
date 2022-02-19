#ifndef SALERNOS_CORE_KERNEL_EFIMEM
#define SALERNOS_CORE_KERNEL_EFIMEM

    #include "kerntypes.h"


    typedef struct EfiMemoryDescriptor {
        uint32_t _Type;
        void*    _PhysicalAddress;
        void*    _VirtualAddress;
        uint64_t _Pages;
        uint64_t _Attributes;
    } efimemdesc_t;

    typedef struct MemoryInfo {
        void*    _MemoryMap;
        uint64_t _MemoryMapSize;
        uint64_t _DescriptorSize;
    } meminfo_t;


    /*****************************************************************************************************************************
          RET TYPE          FUNCTION NAME                       FUNCTION ARGUMENTS
    *****************************************************************************************************************************/
          void              kernel_memory_init                  (meminfo_t __meminfo);
          void              kernel_memory_get_sizes             (uint64_t* __memsize, uint64_t* __usablemem, uint64_t* __bootmem);
          void              kernel_memory_get_largest_segment   (uint64_t* __sz, void** __seg);
          efimemdesc_t*     kernel_memory_get_map_entry         (const uint64_t __idx);
    const int               kernel_memory_get_map_entries       ();

#endif
