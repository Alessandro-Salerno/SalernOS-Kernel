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


#ifndef SALERNOS_CORE_KERNEL_HEAP
#define SALERNOS_CORE_KERNEL_HEAP

    #include <kerntypes.h>


    typedef struct HeapSegmentHeader {
        size_t                    _Length;
        struct HeapSegmentHeader* _Next;
        struct HeapSegmentHeader* _Last;
        bool                      _Free;
    } heapseghdr_t;


    /****************************************************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    ****************************************************************************************************/
    void            kernel_heap_initialize        (void* __heapbase, size_t __pgcount);
    void*           kernel_heap_allocate          (size_t __buffsize);
    void            kernel_heap_free              (void* __buff);

#endif
