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
