/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2022 Alessandro Salerno

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**********************************************************************/


#include "Memory/pgfalloc.h"
#include "Memory/paging.h"
#include "kernelpanic.h"
#include <kmem.h>


static pgtm_t tableManager;


static pgtable_t* __init_page_table__(pgtable_t* __dir, uint64_t __idx) {
    pgdirent_t _entry = __dir->_Entries[__idx];
    pgtable_t* _pgtable;
    
    if (!_entry._Present) {
        _pgtable = (pgtable_t*)(kernel_pgfa_page_new());
        kmemset((void*)(_pgtable), 4096, 0);
        
        _entry._Address   = (uint64_t)(_pgtable) >> 12;
        _entry._Present   = TRUE;
        _entry._ReadWrite = TRUE;

        __dir->_Entries[__idx] = _entry;
        return _pgtable;
    }

    return (pgtable_t*)((uint64_t)(_entry._Address) << 12);
}


pgtm_t kernel_paging_initialize(pgtable_t* __lvl4) {
    tableManager = (pgtm_t) {
        ._PML4Address = __lvl4
    };

    return tableManager;
}

pgmapidx_t kernel_paging_index(uint64_t __virtaddr) {
    return (pgmapidx_t) {
        ._PageDirectoryPointerIndex = (__virtaddr >> 39) & 0x1ff,
        ._PageDirectoryIndex        = (__virtaddr >> 30) & 0x1ff,
        ._PageTableIndex            = (__virtaddr >> 21) & 0x1ff,
        ._PageIndex                 = (__virtaddr >> 12) & 0x1ff
    };
}

void kernel_paging_address_map(pgtm_t* __manager, void* __virtaddr, void* __physaddr) {
    pgmapidx_t _indexer = kernel_paging_index((uint64_t)(__virtaddr));
    
    pgtable_t* _page_directory_pointer = __init_page_table__(__manager->_PML4Address, _indexer._PageDirectoryPointerIndex);
    pgtable_t* _page_directory         = __init_page_table__(_page_directory_pointer, _indexer._PageDirectoryIndex);
    pgtable_t* _page_table             = __init_page_table__(_page_directory, _indexer._PageTableIndex); 

    pgdirent_t _entry = _page_table->_Entries[_indexer._PageIndex];
    _entry._Address   = (uint64_t)(__physaddr) >> 12;
    _entry._Present   = TRUE;
    _entry._ReadWrite = TRUE;
    
    _page_table->_Entries[_indexer._PageIndex] = _entry;
}

void kernel_paging_address_mapn(pgtm_t* __manager, void* __base, size_t __sz) {
     for (uint64_t _addr = (uint64_t)(__base); _addr < (uint64_t)(__base) + __sz; _addr += 4096)
        kernel_paging_address_map(__manager, (void*)(_addr), (void*)(_addr));
}
