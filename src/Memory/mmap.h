#ifndef SALERNOS_CORE_KERNEL_MMAP
#define SALERNOS_CORE_KERNEL_MMAP

    #include <kerntypes.h>

    #define USABLE_MEM_TYPE 7


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

    typedef struct MemorySegment {
        void*    _Segment;
        uint64_t _Size;
    } memseg_t;


    /********************************************************************************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    ********************************************************************************************************************************/
    void            kernel_mmap_initialize        (meminfo_t __meminfo);
    void            kernel_mmap_info_get          (uint64_t* __memsz, uint64_t* __usablemem, memseg_t* __lseg, meminfo_t* __meminfo);
    efimemdesc_t*   kernel_mmap_entry_get         (uint64_t __idx);

#endif
