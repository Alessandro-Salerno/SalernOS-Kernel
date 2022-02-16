#include "Memory/paging.h"
#include "Memory/palloc.h"


static pgtable_t* __init_page_table__(pgtable_t* __dir, uint64_t __idx) {
    pgdirent_t _entry = __dir->_Entries[__idx];
    pgtable_t* _pgtable;
    
    if (!_entry._Present) {
        _pgtable = (pgtable_t*)(kernel_allocator_allocate_page());
        kernel_memory_set((void*)(_pgtable), 4096, 0);
        
        _entry._Address   = (uint64_t)(_pgtable) >> 12;
        _entry._Present   = 1;
        _entry._ReadWrite = 1;

        __dir->_Entries[__idx] = _entry;
        return _pgtable;
    }

    return (pgtable_t*)((uint64_t)(_entry._Address) << 12);
}

pgmapidx_t kernel_paging_index(uint64_t __virtaddr) {
    return (pgmapidx_t) {
        ._PageDirectoryPointerIndex = (__virtaddr >> 39) & 0x1ff,
        ._PageDirectoryIndex        = (__virtaddr >> 30) & 0x1ff,
        ._PageTableIndex            = (__virtaddr >> 21) & 0x1ff,
        ._PageIndex                 = (__virtaddr >> 12) & 0x1ff
    };
}

void kernel_paging_map_address(pgtm_t* __manager, void* __virtaddr, void* __physaddr) {
    pgmapidx_t _indexer = kernel_paging_index((uint64_t)(__virtaddr));
    
    pgtable_t* _page_directory_pointer = __init_page_table__(__manager->_PML4Address, _indexer._PageDirectoryPointerIndex);
    pgtable_t* _page_directory         = __init_page_table__(_page_directory_pointer, _indexer._PageDirectoryIndex);
    pgtable_t* _page_table             = __init_page_table__(_page_directory, _indexer._PageTableIndex); 

    pgdirent_t _entry = _page_table->_Entries[_indexer._PageIndex];
    _entry._Address   = (uint64_t)(__physaddr) >> 12;
    _entry._Present   = 1;
    _entry._ReadWrite = 1;
    
    _page_table->_Entries[_indexer._PageIndex] = _entry;
}
