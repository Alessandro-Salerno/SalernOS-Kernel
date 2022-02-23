#ifndef SALERNOS_CORE_KERNEL_PAGING
#define SALERNOS_CORE_KERNEL_PAGING

    #include <kerntypes.h>


    typedef struct PageDirectoryEntry {
        bool_t   _Present       : 1;
        bool_t   _ReadWrite     : 1;
        bool_t   _UserSuper     : 1;
        bool_t   _WriteThrough  : 1;
        bool_t   _CacheDisabled : 1;
        bool_t   _Accessed      : 1;
        bool_t   _IgnoreZero    : 1;
        bool_t   _LargerPages   : 1;
        bool_t   _IgnoreOne     : 1;
        uint8_t  _Available     : 3;
        uint64_t _Address       : 40;
    } pgdirent_t;

    typedef struct __attribute__((__aligned__(0x1000))) PageTable {
        pgdirent_t _Entries[512];
    } pgtable_t;

    typedef struct PageMapIndexerData {
        uint64_t _PageDirectoryPointerIndex;
        uint64_t _PageDirectoryIndex;
        uint64_t _PageTableIndex;
        uint64_t _PageIndex;
    } pgmapidx_t;

    typedef struct PageTableManagerData {
        pgtable_t* _PML4Address;
    } pgtm_t;


    /*****************************************************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    *****************************************************************************************************/
    pgmapidx_t      kernel_paging_index           (uint64_t __virtaddr);
    void            kernel_paging_address_map     (pgtm_t* __manager, void* __virtaddr, void* __physaddr);
    void            kernel_paging_address_mapn    (pgtm_t* __manager, void* __base, size_t __sz);

#endif
