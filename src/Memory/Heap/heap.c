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


#include "Memory/Heap/heap.h"
#include "Memory/pgfalloc.h"
#include "Memory/paging.h"
#include "kernelpanic.h"
#include <kmath.h>


static void* heapBase;
static void* heapEnd;
static heapseghdr_t* lastHeader;


static void __combine_forward__(heapseghdr_t* __segment) {
    SOFTASSERT(__segment->_Next != NULL, RETVOID);
    SOFTASSERT(__segment->_Next->_Free, RETVOID);

    if (__segment->_Next == lastHeader) lastHeader = __segment;

    if (__segment->_Next->_Next != NULL)
        __segment->_Next->_Next->_Last = __segment;

    __segment->_Length = __segment->_Length + __segment->_Next->_Length + sizeof(heapseghdr_t);
    __segment->_Next   = __segment->_Next->_Next;
}

static void __combine_backward__(heapseghdr_t* __segment) {
    SOFTASSERT(__segment->_Last != NULL && __segment->_Last->_Free, RETVOID);
    __combine_forward__(__segment->_Last);
}

static heapseghdr_t* __split__(heapseghdr_t* __segment, size_t __size) {
    SOFTASSERT(__size >= 0x10, NULL);

    int64_t _split_seg_len = __segment->_Length - __size - sizeof(heapseghdr_t);

    SOFTASSERT(_split_seg_len >= 0x10, NULL);

    heapseghdr_t* _split_hdr = (heapseghdr_t*)((size_t)(__segment) + __size + sizeof(heapseghdr_t));
    __segment->_Next->_Last  = _split_hdr;
    _split_hdr->_Next        = __segment->_Next;
    __segment->_Next         = _split_hdr;
    _split_hdr->_Last        = __segment;
    _split_hdr->_Length      = _split_seg_len;
    _split_hdr->_Free        = __segment->_Free;
    __segment->_Length       = __size;

    if (lastHeader == __segment) lastHeader = _split_hdr;
    return _split_hdr;
}

static void __extend__(size_t __size) {
    __size = kroundl(__size, 4096);

    size_t _npages = __size / 4096;
    heapseghdr_t* _new_seg = (heapseghdr_t*)(heapEnd);

    for (size_t _i = 0; _i < _npages; _i++) {
        kernel_paging_address_map(heapEnd, kernel_pgfa_page_new());
        heapEnd = (void*)((size_t)(heapEnd) + 4096);
    }

    _new_seg->_Free   = TRUE;
    _new_seg->_Last   = lastHeader;
    lastHeader->_Next = _new_seg;
    lastHeader        = _new_seg;
    _new_seg->_Next   = NULL;
    _new_seg->_Length = __size - sizeof(heapseghdr_t);
    __combine_backward__(_new_seg);
}


void kernel_heap_initialize(void* __heapbase, size_t __pgcount) {
    void* _pos = __heapbase;

    for (size_t _i = 0; _i < __pgcount; _i++) {
        kernel_paging_address_map(_pos, kernel_pgfa_page_new());
        _pos = (void*)((size_t)(_pos) + 4096);
    }

    size_t _heap_size = __pgcount * 4096;

    heapBase = __heapbase;
    heapEnd  = (void*)((size_t)(heapBase) + _heap_size);

    heapseghdr_t* _start_seg = (heapseghdr_t*)(heapBase);
    _start_seg->_Length      = _heap_size - sizeof(heapseghdr_t);
    _start_seg->_Next        = NULL; 
    _start_seg->_Last        = NULL;
    _start_seg->_Free        = TRUE;
    lastHeader               = _start_seg;
}

void* kernel_heap_allocate(size_t __buffsize) {
    SOFTASSERT(__buffsize != 0, NULL);
    
    __buffsize = kroundl(__buffsize, 0x10);

    heapseghdr_t* _current_seg = (heapseghdr_t*)(heapBase);

    while (TRUE) {
        if (_current_seg->_Free) {
            if (_current_seg->_Length > __buffsize) {
                __split__(_current_seg, __buffsize);
                _current_seg->_Free = FALSE; 
                return (void*)((uint64_t)(_current_seg) + sizeof(heapseghdr_t));
            }
            
            if (_current_seg->_Length == __buffsize) {
                _current_seg->_Free = FALSE; 
                return (void*)((uint64_t)(_current_seg) + sizeof(heapseghdr_t));
            }
        }

        if (_current_seg->_Next == NULL) break;

        _current_seg = _current_seg->_Next;
    }

    __extend__(__buffsize);
    return kernel_heap_allocate(__buffsize);
}

void kernel_heap_free(void* __buff) {
    heapseghdr_t* _segment = (heapseghdr_t*)(__buff) - 1;
    _segment->_Free        = TRUE;
    
    __combine_forward__(_segment);
    __combine_backward__(_segment);
}
